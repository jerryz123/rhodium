// Attributes per-thread CPU samples to exact ELF function ranges and simulator phases.
#include <elf.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <vector>
#include <cstring>
using Json=nlohmann::json;
struct Symbol{uint64_t address,size;std::string name;};
static std::vector<Symbol> symbols(const std::string&path){std::ifstream f(path,std::ios::binary);if(!f)return {};Elf64_Ehdr h{};f.read(reinterpret_cast<char*>(&h),sizeof h);if(memcmp(h.e_ident,ELFMAG,SELFMAG)||h.e_ident[EI_CLASS]!=ELFCLASS64||h.e_shentsize!=sizeof(Elf64_Shdr))return {};std::vector<Elf64_Shdr>sections(h.e_shnum);f.seekg(h.e_shoff);f.read(reinterpret_cast<char*>(sections.data()),sections.size()*sizeof(Elf64_Shdr));std::vector<Symbol>out;
 for(const auto&s:sections)if(s.sh_type==SHT_SYMTAB&&s.sh_link<sections.size()){auto strings=sections[s.sh_link];std::string names(strings.sh_size,'\0');f.seekg(strings.sh_offset);f.read(names.data(),names.size());std::vector<Elf64_Sym>syms(s.sh_size/sizeof(Elf64_Sym));f.seekg(s.sh_offset);f.read(reinterpret_cast<char*>(syms.data()),syms.size()*sizeof(Elf64_Sym));for(const auto&symbol:syms)if(ELF64_ST_TYPE(symbol.st_info)==STT_FUNC&&symbol.st_size&&symbol.st_name<names.size())out.push_back({symbol.st_value,symbol.st_size,names.c_str()+symbol.st_name});}
 std::sort(out.begin(),out.end(),[](const auto&a,const auto&b){return a.address<b.address;});return out;
}
struct Mapping{uint64_t low,high,offset;std::string path;};
int main(int argc,char**argv){try{if(argc!=2)throw std::runtime_error("sample prefix");std::string prefix=argv[1];std::ifstream maps(prefix+".maps");if(!maps)throw std::runtime_error("missing maps");std::vector<Mapping>ranges;std::string line;std::map<std::string,std::vector<Symbol>>tables;
 while(std::getline(maps,line)){std::istringstream in(line);std::string range,permissions,offset,device,inode,path;in>>range>>permissions>>offset>>device>>inode;std::getline(in,path);size_t first=path.find_first_not_of(' ');if(first==std::string::npos)continue;path=path.substr(first);if(path[0]!='/')continue;auto dash=range.find('-');ranges.push_back({std::stoull(range.substr(0,dash),nullptr,16),std::stoull(range.substr(dash+1),nullptr,16),std::stoull(offset,nullptr,16),path});}
 std::map<std::string,uint64_t>totals;std::map<std::pair<std::string,uint64_t>,uint64_t>instructions;Json threads=Json::object(),thread_functions=Json::object();uint64_t total=0,files=0;
 auto parent=std::filesystem::path(prefix).parent_path();if(parent.empty())parent=".";auto stem=std::filesystem::path(prefix).filename().string()+".";
 for(const auto&entry:std::filesystem::directory_iterator(parent)){auto name=entry.path().filename().string();if(name.rfind(stem,0)||entry.path().extension()!=".samples")continue;std::ifstream input(entry.path());std::getline(input,line);unsigned expected=0,lost=0;if(sscanf(line.c_str(),"# samples=%u lost=%u",&expected,&lost)!=2||lost)throw std::runtime_error("lost or invalid sample records");uint64_t count=0;
  while(std::getline(input,line)){uint64_t pc=std::stoull(line,nullptr,16);std::string key="unmapped";for(const auto&r:ranges)if(pc>=r.low&&pc<r.high){uint64_t relative=pc-r.low+r.offset;if(!tables.count(r.path))tables[r.path]=symbols(r.path);auto&table=tables[r.path];auto it=std::upper_bound(table.begin(),table.end(),relative,[](uint64_t x,const Symbol&s){return x<s.address;});key=std::filesystem::path(r.path).filename().string()+":unknown";if(it!=table.begin()){--it;if(relative-it->address<it->size)key=std::filesystem::path(r.path).filename().string()+":"+it->name;}++instructions[{r.path,relative}];break;}++totals[key];auto&tf=thread_functions[name];if(tf.is_null())tf=Json::object();tf[key]=tf.value(key,uint64_t(0))+1;++total;++count;}
  if(count!=expected)throw std::runtime_error("truncated sample records");threads[name]=count;++files;
 }
 if(!files||!total)throw std::runtime_error("no samples");std::vector<std::pair<std::string,uint64_t>>ordered(totals.begin(),totals.end());std::sort(ordered.begin(),ordered.end(),[](const auto&a,const auto&b){return a.second>b.second;});Json ranked=Json::array();for(auto&[name,count]:ordered)ranked.push_back({{"function",name},{"samples",count},{"percent",100.*count/total}});
 std::vector<std::pair<std::pair<std::string,uint64_t>,uint64_t>>pcs(instructions.begin(),instructions.end());std::sort(pcs.begin(),pcs.end(),[](const auto&a,const auto&b){return a.second>b.second;});Json hot=Json::array();for(size_t i=0;i<std::min<size_t>(64,pcs.size());++i){const auto&[location,count]=pcs[i];std::ostringstream address;address<<std::hex<<location.second;hot.push_back({{"path",location.first},{"elf_offset",address.str()},{"samples",count},{"percent",100.*count/total}});}
 std::cout<<Json({{"total_samples",total},{"threads",threads},{"thread_functions",thread_functions},{"functions",ranked},{"hot_instructions",hot}}).dump(2)<<'\n';
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
