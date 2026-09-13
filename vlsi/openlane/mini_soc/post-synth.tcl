# Balances MiniSoC's Sky130 RV64 arithmetic capture cones and proves each replacement before handoff.
# SPDX-License-Identifier: Apache-2.0

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
set repairs {
    divider_repair {rv5stage2Fcore2Fdivider2Fdivider2Fresponse_bits} 2000
}
# Opt in only: shared side-output remapping can regress other timing paths.
if {$::env(MINI_SOC_REPAIR_MULTIPLIER) ni {0 1}} {
    error "MINI_SOC_REPAIR_MULTIPLIER must be 0 or 1"
}
if {$::env(MINI_SOC_REPAIR_MULTIPLIER)} {
    lappend repairs multiplier_repair {rv5stage2Fcore2Fmultiplier2Fmultiplier2Fmultiplicand rv5stage2Fcore2Fmultiplier2Fmultiplier2Fmultiplier} 4000
}
foreach {repair registers limit} $repairs {
    yosys select -module MiniSoC
    set endpoints {}
    foreach register $registers {
        for {set bit 0} {$bit < 64} {incr bit} {
            lappend endpoints "w:${register}\[$bit\]"
        }
    }
    yosys select -set endpoints {*}$endpoints %% %ci1 t:sky130_fd_sc_hd__dfxtp_2 %i
    yosys select -assert-count [expr {64 * [llength $registers]}] @endpoints
    # Stop at capture/launch flops; submod retains every shared side output too.
    yosys select -set cone @endpoints {%x:+[D]} @endpoints %d %ci*:-sky130_fd_sc_hd__dfxtp_2 t:sky130_fd_sc_hd__* %i t:sky130_fd_sc_hd__dfxtp_2 %d
    yosys select -assert-min 65 @cone
    yosys select -assert-max $limit @cone
    yosys submod -hidden -name $repair @cone
    yosys select -clear
    yosys stat $repair
    yosys select * $repair %d
    yosys write_rtlil -selected "\"$out/$repair-outside-before.il\""
    yosys select -clear
    yosys techmap -map %models $repair
    yosys techmap $repair
    yosys opt_clean $repair
    yosys design -copy-to reference $repair
    # Inline ABC policy keeps balancing local and uses the synthesis clock/load/library.
    yosys abc -script "+strash;balance,-d;&get,-n;&dch;&nf,-D,$delay;&put;buffer,-c,-N,10;topo;upsize,-c,-D,$delay;dnsize,-c,-D,$delay;stime,-p" -constr "\"$out/mapping.sdc\"" {*}$libargs $repair
    yosys stat $repair
    yosys select * $repair %d
    yosys write_rtlil -selected "\"$out/$repair-outside-after.il\""
    yosys select -clear
    # Only Yosys's global name allocator may change outside the extracted module.
    exec diff -I {^autoidx [0-9][0-9]*$} "$out/$repair-outside-before.il" "$out/$repair-outside-after.il"
    yosys design -save mapped

    yosys design -reset
    yosys design -copy-from reference -as gold $repair
    yosys design -copy-from mapped -as gate $repair
    yosys techmap -map %models gate
    yosys techmap gate
    yosys miter -equiv -flatten gold gate proof
    yosys select proof
    yosys sat -verify -prove trigger 0 -timeout 30

    yosys design -load mapped
    yosys flatten MiniSoC/$repair
    yosys select -assert-none MiniSoC/t:$repair
    yosys delete $repair
}
yosys check -assert
yosys write_verilog -noattr -noexpr -nohex -nodec "\"$out/MiniSoC.nl.v\""
yosys write_json "\"$out/MiniSoC.nl.v.json\""
