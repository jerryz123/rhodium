// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "../../events/collector.h"
#include <istream>
#include <ostream>
#include <stdexcept>

// Lossless test-only transfer of completed instruction records from Verilator
// to the native Sail test. No expected data or DUT memory is copied to Sail.
namespace vector_test {
inline void save(std::ostream& out, const rhodium::cosim::observation::Record& r) {
  namespace o = rhodium::cosim::observation;
  const auto& i=std::get<o::Instruction>(r.event);
  out << r.instance << ' ' << r.hart << ' ' << r.sample << ' ' << r.id.epoch << ' ' << r.id.order << ' '
      << i.pc << ' ' << i.encoding << ' ' << i.encoding_valid_bytes << ' ' << i.instruction_bytes << ' '
      << i.privilege.mode << ' ' << i.privilege.virtualized << ' ' << i.producers << ' ';
  if(const auto* retired=std::get_if<o::Retirement>(&r.outcome))
    out << 'R' << ' ' << retired->next_pc << ' ' << retired->privilege.mode << ' ' << retired->privilege.virtualized << ' ';
  else {
    const auto& t=std::get<o::Trap>(r.outcome);
    out << 'T' << ' ' << t.cause << ' ' << t.epc << ' ' << t.tval << ' ' << t.target_pc << ' '
        << t.privilege.mode << ' ' << t.privilege.virtualized << ' ' << t.guest_valid << ' ' << t.htval << ' ' << t.htinst << ' ';
  }
  out << r.environment.interrupt_inputs << ' ' << r.environment.time << ' ' << r.environment.cycle << ' '
      << r.environment.interrupt_boundary << ' ' << r.effects.size();
  for(const auto& [id,e]:r.effects) {
    out << ' ' << id.producer << ' ' << id.index << ' ';
    if(const auto* w=std::get_if<o::RegisterWrite>(&e))
      out << 'R' << ' ' << unsigned(w->bank) << ' ' << w->index << ' ' << w->bit_offset << ' ' << w->mask << ' ' << w->value;
    else if(const auto* c=std::get_if<o::CsrUpdate>(&e))
      out << 'C' << ' ' << c->address << ' ' << unsigned(c->operation) << ' ' << c->mask << ' ' << c->value;
    else {
      const auto& m=std::get<o::MemoryEffect>(e);
      out << 'M' << ' ' << m.access_id << ' ' << m.fragment_offset << ' ' << unsigned(m.kind) << ' '
          << m.virtual_address << ' ' << m.physical_valid << ' ' << m.physical_address << ' ' << m.byte_mask << ' '
          << m.read_valid << ' ' << m.write_valid << ' ' << m.read_data << ' ' << m.write_data << ' ' << unsigned(m.result);
    }
  }
  out << '\n';
  if(!out) throw std::runtime_error("writing RTL vector records");
}
inline bool read(std::istream& in, rhodium::cosim::observation::Record& r) {
  namespace o = rhodium::cosim::observation;
  r={}; o::Instruction i{}; std::size_t count; char outcome;
  if(!(in>>r.instance)) { if(in.eof()) return false; throw std::runtime_error("reading RTL records"); }
  in >> r.hart >> r.sample >> r.id.epoch >> r.id.order
     >> i.pc >> i.encoding >> i.encoding_valid_bytes >> i.instruction_bytes
     >> i.privilege.mode >> i.privilege.virtualized >> i.producers >> outcome;
  if(outcome=='R') {
    o::Retirement retired{}; in >> retired.next_pc >> retired.privilege.mode >> retired.privilege.virtualized;
    r.outcome=retired;
  } else if(outcome=='T') {
    o::Trap t{}; in >> t.cause >> t.epc >> t.tval >> t.target_pc >> t.privilege.mode >> t.privilege.virtualized >> t.guest_valid >> t.htval >> t.htinst;
    r.outcome=t;
  } else throw std::runtime_error("unknown RTL outcome");
  in >> r.environment.interrupt_inputs >> r.environment.time >> r.environment.cycle >> r.environment.interrupt_boundary >> count;
  r.event=i;
  for(std::size_t n=0;n<count && in;++n) {
    o::EffectId id{}; char type; unsigned a,b;
    in>>id.producer>>id.index>>type;
    if(type=='R') {
      o::RegisterWrite w{}; in>>a>>w.index>>w.bit_offset>>w.mask>>w.value; w.bank=o::Bank(a); r.effects[id]=w;
    } else if(type=='C') {
      o::CsrUpdate c{}; in>>c.address>>a>>c.mask>>c.value; c.operation=o::CsrOperation(a); r.effects[id]=c;
    } else if(type=='M') {
      o::MemoryEffect m{};
      in>>m.access_id>>m.fragment_offset>>a>>m.virtual_address>>m.physical_valid>>m.physical_address>>m.byte_mask
        >>m.read_valid>>m.write_valid>>m.read_data>>m.write_data>>b;
      m.kind=o::AccessKind(a); m.result=o::AccessResult(b); r.effects[id]=m;
    } else throw std::runtime_error("unknown RTL effect");
  }
  if(!in) throw std::runtime_error("truncated RTL vector record");
  return true;
}
}
