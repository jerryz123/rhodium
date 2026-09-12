// Ranks actual Clang spill slots and correlates them with addressed load samples.
#include <algorithm>
#include <cstring>
#include <elf.h>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
using Json = nlohmann::json;
struct Slot { unsigned stores=0,loads=0,other=0,span=0; };
struct Function { bool calls=false;std::set<unsigned> vectors;std::map<int,Slot> slots; };
static std::ifstream read(const char *path) {
  std::ifstream f(path);if(!f)throw std::runtime_error(std::string("cannot read ")+path);return f;
}
int main(int argc,char **argv) {
  try {
    if(argc!=5)throw std::runtime_error("usage: scratch-inspect clang.s profile.json disassembly.txt report.json");
    auto assembly=read(argv[1]);std::string line,name;
    std::map<std::string,Function> functions;
    const std::regex begin(R"(^\s*\.type\s+([^,]+),@function)"),
      vector(R"(%[xyz]mm(\d+))"),stack(R"((-?\d*)\(%rsp\))"),move(R"(^\s*movq\s+)"),
      width(R"(# (\d+)-byte)");
    while(std::getline(assembly,line)) {
      std::smatch m;
      if(std::regex_search(line,m,begin))name=m[1];
      if(name.empty())continue;
      auto &f=functions[name];
      if(line.find("\tcall")!=std::string::npos)f.calls=true;
      for(auto i=std::sregex_iterator(line.begin(),line.end(),vector);i!=std::sregex_iterator();++i)
        f.vectors.insert(std::stoul((*i)[1]));
      for(auto i=std::sregex_iterator(line.begin(),line.end(),stack);i!=std::sregex_iterator();++i) {
        auto offset=(*i)[1].str();auto &s=f.slots[offset.empty()?0:std::stoi(offset)];
        std::smatch bytes;
        s.span=std::max(s.span,std::regex_search(line,bytes,width)?unsigned(std::stoul(bytes[1])):64u);
        if(std::regex_search(line,move)&&line.find("# 8-byte Spill")!=std::string::npos)++s.stores;
        else if(std::regex_search(line,move)&&line.find("# 8-byte Reload")!=std::string::npos)++s.loads;
        else ++s.other;
      }
      if(line.find("\t.size\t"+name+",")==0)name.clear();
    }
    Json profile;read(argv[2])>>profile;
    auto binary=read(profile.at("library").get<std::string>().c_str());
    Elf64_Ehdr header{};
    if(!binary.read(reinterpret_cast<char*>(&header),sizeof header)||
       std::memcmp(header.e_ident,ELFMAG,SELFMAG)||header.e_ident[EI_CLASS]!=ELFCLASS64||
       header.e_ident[EI_DATA]!=ELFDATA2LSB||header.e_machine!=EM_X86_64||
       header.e_phentsize!=sizeof(Elf64_Phdr))throw std::runtime_error("expected an x86-64 ELF library");
    std::vector<Elf64_Phdr> segments;
    for(unsigned i=0;i<header.e_phnum;++i){
      Elf64_Phdr segment{};binary.seekg(header.e_phoff+uint64_t(i)*header.e_phentsize);
      if(!binary.read(reinterpret_cast<char*>(&segment),sizeof segment))throw std::runtime_error("truncated ELF program headers");
      if(segment.p_type==PT_LOAD)segments.push_back(segment);
    }
    auto disassembly=read(argv[3]);std::map<uint64_t,std::pair<std::string,int>> addresses;
    std::map<uint64_t,std::string> mnemonics;
    const std::regex label(R"(^[0-9a-f]+ <([^>]+)>:.*)"),instruction(R"(^\s*([0-9a-f]+):\s+(\w+).*)"),
      rsp(R"(\[rsp(?:([+-])0x([0-9a-f]+))?\])");
    name.clear();
    while(std::getline(disassembly,line)) {
      std::smatch m,mem;
      if(std::regex_match(line,m,label)){name=m[1];continue;}
      if(!std::regex_match(line,m,instruction))continue;
      mnemonics.emplace(std::stoull(m[1],nullptr,16),m[2]);
      if(std::regex_search(line,mem,rsp)) {
        int offset=mem[2].matched?std::stoi(mem[2],nullptr,16):0;if(mem[1]=="-")offset=-offset;
        addresses.emplace(std::stoull(m[1],nullptr,16),std::pair{name,offset});
      }
    }
    // Convert procfs file offsets through PT_LOAD before matching disassembler virtual addresses.
    std::map<std::pair<std::string,int>,unsigned> observed;
    std::map<std::string,unsigned> stack_opcodes;
    uint64_t all_loads=0,stack_loads=0,mapped_stack_loads=0;
    for(const auto &sample:profile.at("samples")) {
      ++all_loads;if(sample.at("region")!="stack")continue;++stack_loads;
      uint64_t ip=sample.at("ip");
      for(const auto &mapping:profile.at("maps")) {
        if(mapping.at("path")!=profile.at("library"))continue;
        uint64_t start=mapping.at("start"),end=mapping.at("end");
        if(ip<start||ip>=end)continue;
        uint64_t file_ip=ip-start+mapping.at("file_offset").get<uint64_t>();
        uint64_t address=0;bool mapped=false;
        for(const auto &segment:segments)if(file_ip>=segment.p_offset&&file_ip-segment.p_offset<segment.p_filesz){
          address=segment.p_vaddr+file_ip-segment.p_offset;mapped=true;break;
        }
        if(!mapped)break;
        auto opcode=mnemonics.find(address);
        if(opcode!=mnemonics.end())++stack_opcodes[opcode->second];
        auto found=addresses.find(address);
        if(found!=addresses.end()){++observed[found->second];++mapped_stack_loads;}
        break;
      }
    }
    Json result={{"note","Static references are not dynamic spill counts. Sample correlations require assembly/disassembly from the profiled library. XMM/YMM/ZMM aliases count as one register index."},
      {"library",profile.at("library")},{"library_sha256",profile.at("library_sha256")},
      {"all_load_samples",all_loads},{"stack_load_samples",stack_loads},
      {"mapped_stack_load_samples",mapped_stack_loads},{"stack_opcode_samples",stack_opcodes},
      {"functions",Json::array()}};
    unsigned eligible_samples=0,eligible_stores=0,eligible_reloads=0;
    for(const auto &[fn,f]:functions) {
      if(f.slots.empty())continue;
      Json slots=Json::array();std::vector<unsigned> free;
      for(unsigned i=0;i<32;++i)if(!f.vectors.count(i))free.push_back(i);
      for(const auto &[offset,s]:f.slots) {
        bool overlap=std::any_of(f.slots.begin(),f.slots.end(),[&](const auto &other){return other.first!=offset&&other.first<offset+8&&offset<other.first+int(other.second.span);});
        bool eligible=fn.starts_with("bound_")&&!f.calls&&!overlap&&!s.other&&s.stores&&s.loads>=2;
        unsigned samples=observed[{fn,offset}];
        slots.push_back({{"offset",offset},{"stores",s.stores},{"reloads",s.loads},
          {"other_references",s.other},{"span",s.span},{"sampled_loads",samples},{"eligible",eligible}});
        if(eligible){eligible_samples+=samples;eligible_stores+=s.stores;eligible_reloads+=s.loads;}
      }
      result["functions"].push_back({{"function",fn},{"calls",f.calls},{"used_vector_indices",f.vectors},
        {"unused_vector_indices",free},{"slots",slots}});
    }
    result["eligible_load_samples"]=eligible_samples;result["eligible_static_stores"]=eligible_stores;
    result["eligible_static_reloads"]=eligible_reloads;
    std::ofstream output(argv[4]);if(!output||!(output<<result.dump(2)<<'\n'))throw std::runtime_error("cannot write report");
    std::cout<<"eligible spill samples "<<eligible_samples<<" / "<<all_loads<<" total loads; "
      <<eligible_stores<<" static stores, "<<eligible_reloads<<" static reloads\n";
  } catch(const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}
}
