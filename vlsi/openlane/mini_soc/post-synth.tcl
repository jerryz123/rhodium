# Remaps MiniSoC's Sky130 RV64 divider capture cone and proves the replacement before handoff.

set step [file normalize $::env(MINI_SOC_SYNTH_STEP_DIR)]
set out [file normalize $::env(MINI_SOC_POST_SYNTH_DIR)]
if {$out eq $step || [string first "$out/" "$step/"] == 0} {
    error "output must not contain the original synthesis step"
}
set config "$step/config.json"
set extra "$step/extra.json"
if {[exec jq -er .DESIGN_NAME $config] ne "MiniSoC"} {
    error "expected MiniSoC synthesis input"
}
set model [exec jq -er {[.blackbox_models[] | select(contains("sky130_fd_sc_hd__") and endswith(".lib"))] | if length == 1 then .[0] else error("expected one Sky130 HD Liberty model") end} $extra]
set libs [split [exec jq -er {.libs_synth | if length > 0 then .[] else error("missing synthesis libraries") end} $extra] "\n"]
set delay [exec jq -er {.CLOCK_PERIOD | if type == "number" and . > 0 then . * 1000 else error("invalid clock period") end} $config]
set driver [exec jq -er {.SYNTH_DRIVING_CELL | split("/")[0]} $config]
set load [exec jq -er .OUTPUT_CAP_LOAD $config]
set libargs {}
foreach lib $libs {
    lappend libargs -liberty "\"$lib\""
}
set sdc [open "$out/mapping.sdc" w]
puts $sdc "set_driving_cell $driver\nset_load $load"
close $sdc

yosys read_liberty -ignore_miss_func -ignore_miss_dir -ignore_miss_data_latch -ignore_buses "\"$model\""
yosys design -stash models
yosys read_json "\"$step/MiniSoC.nl.v.json\""
yosys select -module MiniSoC
yosys select -assert-count 128 w:rv5stage2Fcore2Fdivider2Fdivider2Fresponse_bits*
set endpoints {}
for {set bit 0} {$bit < 64} {incr bit} {
    lappend endpoints "w:rv5stage2Fcore2Fdivider2Fdivider2Fresponse_bits\[$bit\]"
}
yosys select -set endpoints {*}$endpoints %% %ci1 t:sky130_fd_sc_hd__dfxtp_2 %i
yosys select -assert-count 64 @endpoints
# Stop at capture/launch flops; submod retains every shared side output too.
yosys select -set cone @endpoints {%x:+[D]} @endpoints %d %ci*:-sky130_fd_sc_hd__dfxtp_2 t:sky130_fd_sc_hd__* %i t:sky130_fd_sc_hd__dfxtp_2 %d
yosys select -assert-min 65 @cone
yosys select -assert-max 2000 @cone
yosys submod -hidden -name divider_repair @cone
yosys select -clear
yosys stat divider_repair
yosys select * divider_repair %d
yosys write_rtlil -selected "\"$out/outside-before.il\""
yosys select -clear
yosys techmap -map %models divider_repair
yosys techmap divider_repair
yosys opt_clean divider_repair
yosys design -copy-to reference divider_repair
# Inline ABC policy keeps balancing local and uses the synthesis clock/load/library.
yosys abc -script "+strash;balance,-d;&get,-n;&dch;&nf,-D,$delay;&put;buffer,-c,-N,10;topo;upsize,-c,-D,$delay;dnsize,-c,-D,$delay;stime,-p" -constr "\"$out/mapping.sdc\"" {*}$libargs divider_repair
yosys stat divider_repair
yosys select * divider_repair %d
yosys write_rtlil -selected "\"$out/outside-after.il\""
yosys select -clear
# Only Yosys's global name allocator may change outside the extracted module.
exec diff -I {^autoidx [0-9][0-9]*$} "$out/outside-before.il" "$out/outside-after.il"
yosys design -save mapped

yosys design -reset
yosys design -copy-from reference -as gold divider_repair
yosys design -copy-from mapped -as gate divider_repair
yosys techmap -map %models gate
yosys techmap gate
yosys miter -equiv -flatten gold gate proof
yosys select proof
yosys sat -verify -prove trigger 0 -timeout 30

yosys design -load mapped
yosys flatten MiniSoC/divider_repair
yosys select -assert-none MiniSoC/t:divider_repair
yosys delete divider_repair
yosys check -assert
yosys write_verilog -noattr -noexpr -nohex -nodec "\"$out/MiniSoC.nl.v\""
yosys write_json "\"$out/MiniSoC.nl.v.json\""
