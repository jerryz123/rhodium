// Selects a scoped HW hierarchy and retargets policy-selected FIRRTLMem occurrences to SRAM externs.
// SPDX-License-Identifier: Apache-2.0

#include "circt/Dialect/HW/HWOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassRegistry.h"
#include "mlir/Tools/Plugins/PassPlugin.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/YAMLTraits.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

using namespace circt;
using namespace mlir;

namespace {

struct MemoryPolicy {
  unsigned schemaVersion = 0;
  std::string top;
  std::string defaultDecision;
  std::map<std::string, std::string> sites;
};

struct MemorySite {
  std::string path;
  std::string instancePath;
  std::string decision;
  std::string sourceModule;
  std::string wrapperModule;
  hw::InstanceOp instance;
  hw::HWModuleGeneratedOp generator;
};

} // namespace

LLVM_YAML_IS_STRING_MAP(std::string)

namespace llvm::yaml {

template <> struct MappingTraits<MemoryPolicy> {
  static void mapping(IO &io, MemoryPolicy &policy) {
    io.mapRequired("schema_version", policy.schemaVersion);
    io.mapRequired("top", policy.top);
    io.mapRequired("default", policy.defaultDecision);
    io.mapRequired("sites", policy.sites);
  }
};

} // namespace llvm::yaml

namespace {

constexpr llvm::StringLiteral kInferDecision = "infer";
constexpr llvm::StringLiteral kFIRRTLMemSchema = "FIRRTLMem";
constexpr llvm::StringLiteral kSiteAttr = "rhodium.memory.site";
constexpr llvm::StringLiteral kMacroAttr = "rhodium.memory.macro";
constexpr llvm::StringLiteral kSourceAttr = "rhodium.memory.source";

static FailureOr<MemoryPolicy> readPolicy(StringRef path, Operation *anchor) {
  auto buffer = llvm::MemoryBuffer::getFile(path);
  if (!buffer) {
    anchor->emitError() << "cannot read memory policy '" << path
                        << "': " << buffer.getError().message();
    return failure();
  }

  MemoryPolicy policy;
  llvm::yaml::Input input((*buffer)->getBuffer());
  input >> policy;
  if (std::error_code error = input.error()) {
    anchor->emitError() << "cannot parse memory policy '" << path
                        << "': " << error.message();
    return failure();
  }
  if (policy.schemaVersion != 1) {
    anchor->emitError() << "memory policy schema_version must be 1";
    return failure();
  }
  if (policy.top.empty()) {
    anchor->emitError() << "memory policy top must not be empty";
    return failure();
  }
  if (policy.defaultDecision != kInferDecision) {
    anchor->emitError() << "memory policy default must be 'infer'";
    return failure();
  }
  for (const auto &[site, decision] : policy.sites) {
    if (site.empty()) {
      anchor->emitError() << "memory policy contains an empty site path";
      return failure();
    }
    if (decision.empty()) {
      anchor->emitError() << "memory policy decision for '" << site
                          << "' must not be empty";
      return failure();
    }
  }
  return policy;
}

static std::string canonicalSitePath(StringRef instanceName) {
  constexpr llvm::StringLiteral suffix = "_ext";
  if (instanceName.ends_with(suffix))
    instanceName = instanceName.drop_back(suffix.size());
  return instanceName.str();
}

static uint32_t fnv1a(StringRef text) {
  uint32_t hash = 2166136261u;
  for (unsigned char character : text.bytes()) {
    hash ^= character;
    hash *= 16777619u;
  }
  return hash;
}

static std::string wrapperName(StringRef site) {
  std::string result = "rhodium_sram_";
  for (char character : site.take_back(72)) {
    unsigned char byte = static_cast<unsigned char>(character);
    result.push_back(std::isalnum(byte) ? character : '_');
  }
  result += llvm::formatv("_{0:x-8}", fnv1a(site)).str();
  return result;
}

static bool isStructuralAttribute(StringRef name) {
  return name == SymbolTable::getSymbolAttrName() || name == "generatorKind" ||
         name == "module_type" || name == "parameters" ||
         name == "per_port_attrs" || name == "port_locs" ||
         name == "verilogName" || name == SymbolTable::getVisibilityAttrName();
}

static SmallVector<NamedAttribute>
memoryAttributes(hw::HWModuleGeneratedOp generator, StringRef site,
                 StringRef macro, StringRef source) {
  SmallVector<NamedAttribute> attributes;
  for (NamedAttribute attribute : generator->getAttrs())
    if (!isStructuralAttribute(attribute.getName().strref()))
      attributes.push_back(attribute);
  Builder builder(generator.getContext());
  attributes.push_back(
      builder.getNamedAttr(kSiteAttr, builder.getStringAttr(site)));
  attributes.push_back(
      builder.getNamedAttr(kMacroAttr, builder.getStringAttr(macro)));
  attributes.push_back(
      builder.getNamedAttr(kSourceAttr, builder.getStringAttr(source)));
  return attributes;
}

static LogicalResult writeInventory(StringRef path, const MemoryPolicy &policy,
                                    StringRef top, StringRef scopePrefix,
                                    ArrayRef<MemorySite> sites,
                                    Operation *anchor) {
  if (path.empty()) {
    anchor->emitError() << "rhodium-map-memory-sites requires an inventory path";
    return failure();
  }
  llvm::SmallString<256> parent(path);
  llvm::sys::path::remove_filename(parent);
  if (!parent.empty()) {
    std::error_code error = llvm::sys::fs::create_directories(parent);
    if (error) {
      anchor->emitError() << "cannot create memory inventory directory '"
                          << parent << "': " << error.message();
      return failure();
    }
  }
  std::error_code error;
  llvm::raw_fd_ostream output(path, error);
  if (error) {
    anchor->emitError() << "cannot write memory inventory '" << path
                        << "': " << error.message();
    return failure();
  }
  llvm::json::Array siteValues;
  for (const MemorySite &site : sites) {
    llvm::json::Object value{
        {"path", site.path},
        {"instance_path", site.instancePath},
        {"decision", site.decision == kInferDecision ? "infer" : "macro"},
        {"source_module", site.sourceModule},
    };
    if (site.decision != kInferDecision) {
      value["macro"] = site.decision;
      value["wrapper_module"] = site.wrapperModule;
    }
    siteValues.push_back(std::move(value));
  }
  llvm::json::Object inventory{
      {"schema_version", 1},
      {"top", top},
      {"policy_top", policy.top},
      {"scope_prefix", scopePrefix},
      {"default", policy.defaultDecision},
      {"sites", std::move(siteValues)},
  };
  output << llvm::formatv("{0:2}\n", llvm::json::Value(std::move(inventory)));
  return success();
}

struct SelectHWTopPass
    : public PassWrapper<SelectHWTopPass, OperationPass<ModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(SelectHWTopPass)

  SelectHWTopPass() = default;
  SelectHWTopPass(const SelectHWTopPass &other) : PassWrapper(other) {
    top = other.top;
  }

  Option<std::string> top{*this, "top",
                          llvm::cl::desc("HW module to retain as the public top"),
                          llvm::cl::init("")};

  StringRef getArgument() const final { return "rhodium-select-hw-top"; }
  StringRef getDescription() const final {
    return "Make one HW module public and other HW definitions private";
  }

  void runOnOperation() override {
    ModuleOp module = getOperation();
    if (top.empty()) {
      module.emitError() << "rhodium-select-hw-top requires top=<module>";
      return signalPassFailure();
    }
    bool found = false;
    for (hw::HWModuleOp hwModule : module.getOps<hw::HWModuleOp>()) {
      bool isTop = hwModule.getName() == top;
      found |= isTop;
      SymbolTable::setSymbolVisibility(
          hwModule, isTop ? SymbolTable::Visibility::Public
                          : SymbolTable::Visibility::Private);
    }
    if (!found) {
      module.emitError() << "cannot find HW top module '" << top << "'";
      signalPassFailure();
    }
  }
};

struct MapMemorySitesPass
    : public PassWrapper<MapMemorySitesPass, OperationPass<ModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(MapMemorySitesPass)

  MapMemorySitesPass() = default;
  MapMemorySitesPass(const MapMemorySitesPass &other) : PassWrapper(other) {
    policyPath = other.policyPath;
    inventoryPath = other.inventoryPath;
    top = other.top;
    scopePrefix = other.scopePrefix;
  }

  Option<std::string> policyPath{
      *this, "policy", llvm::cl::desc("YAML site-selection policy"),
      llvm::cl::init("")};
  Option<std::string> inventoryPath{
      *this, "inventory", llvm::cl::desc("JSON occurrence inventory output"),
      llvm::cl::init("")};
  Option<std::string> top{
      *this, "top",
      llvm::cl::desc("actual HW top; defaults to the policy's logical top"),
      llvm::cl::init("")};
  Option<std::string> scopePrefix{
      *this, "scope-prefix",
      llvm::cl::desc("flattened instance prefix stripped before policy lookup"),
      llvm::cl::init("")};

  StringRef getArgument() const final { return "rhodium-map-memory-sites"; }
  StringRef getDescription() const final {
    return "Retarget selected FIRRTLMem occurrences to site-specific externs";
  }

  void runOnOperation() override {
    ModuleOp module = getOperation();
    if (policyPath.empty()) {
      module.emitError() << "rhodium-map-memory-sites requires policy=<file>";
      return signalPassFailure();
    }
    FailureOr<MemoryPolicy> policyResult = readPolicy(policyPath, module);
    if (failed(policyResult))
      return signalPassFailure();
    MemoryPolicy policy = std::move(*policyResult);

    if (StringRef(scopePrefix).starts_with("/") ||
        StringRef(scopePrefix).ends_with("/")) {
      module.emitError()
          << "rhodium-map-memory-sites scope-prefix must not start or end with '/'";
      return signalPassFailure();
    }
    StringRef actualTop = top.empty() ? StringRef(policy.top) : StringRef(top);

    SymbolTable symbols(module);
    auto topModule = symbols.lookup<hw::HWModuleOp>(actualTop);
    if (!topModule) {
      module.emitError() << "cannot find mapping top module '" << actualTop
                         << "' after hierarchy preparation";
      return signalPassFailure();
    }

    std::vector<MemorySite> sites;
    llvm::StringSet<> seenPaths;
    bool duplicatePath = false;
    topModule.walk([&](hw::InstanceOp instance) {
      auto generator = symbols.lookup<hw::HWModuleGeneratedOp>(
          instance.getReferencedModuleName());
      if (!generator || generator.getGeneratorKind() != kFIRRTLMemSchema)
        return;
      std::string instancePath = canonicalSitePath(instance.getInstanceName());
      StringRef scopedPath(instancePath);
      if (!scopePrefix.empty()) {
        StringRef prefix(scopePrefix);
        if (!scopedPath.consume_front(prefix) ||
            !scopedPath.consume_front("/") || scopedPath.empty())
          return;
      }
      std::string path = scopedPath.str();
      if (!seenPaths.insert(path).second) {
        instance.emitError() << "duplicate canonical memory site '" << path
                             << "' after flattening";
        duplicatePath = true;
        return;
      }
      auto decision = policy.sites.find(path);
      std::string action = decision == policy.sites.end()
                               ? policy.defaultDecision
                               : decision->second;
      sites.push_back({path, instancePath, action, generator.getName().str(),
                       "", instance, generator});
    });

    if (duplicatePath) {
      signalPassFailure();
      return;
    }
    llvm::StringSet<> available;
    for (const MemorySite &site : sites)
      available.insert(site.path);
    SmallVector<StringRef> unknown;
    for (const auto &[site, decision] : policy.sites)
      if (!available.contains(site))
        unknown.push_back(site);
    if (!unknown.empty()) {
      llvm::sort(unknown);
      InFlightDiagnostic diagnostic = module.emitError()
                                      << "memory policy names unknown site";
      if (unknown.size() != 1)
        diagnostic << "s";
      diagnostic << ": " << llvm::join(unknown, ", ") << "; available sites: ";
      SmallVector<StringRef> names;
      for (const MemorySite &site : sites)
        names.push_back(site.path);
      llvm::sort(names);
      diagnostic << llvm::join(names, ", ");
      signalPassFailure();
      return;
    }

    llvm::sort(sites, [](const MemorySite &left, const MemorySite &right) {
      return left.path < right.path;
    });
    for (hw::HWModuleGeneratedOp generator :
         module.getOps<hw::HWModuleGeneratedOp>())
      if (generator.getGeneratorKind() == kFIRRTLMemSchema)
        SymbolTable::setSymbolVisibility(generator,
                                         SymbolTable::Visibility::Private);
    OpBuilder builder(module.getBodyRegion());
    builder.setInsertionPoint(topModule);
    for (MemorySite &site : sites) {
      if (site.decision == kInferDecision)
        continue;
      site.wrapperModule = wrapperName(site.path);
      if (symbols.lookup(site.wrapperModule)) {
        module.emitError() << "generated SRAM wrapper symbol collision for '"
                           << site.path << "': " << site.wrapperModule;
        return signalPassFailure();
      }
      SmallVector<NamedAttribute> attributes = memoryAttributes(
          site.generator, site.path, site.decision, site.sourceModule);
      hw::HWModuleExternOp external = hw::HWModuleExternOp::create(
          builder, site.generator.getLoc(),
          builder.getStringAttr(site.wrapperModule), site.generator.getPortList(),
          site.wrapperModule, site.generator.getParametersAttr(), attributes);
      symbols.insert(external);
      site.instance.setModuleName(site.wrapperModule);
      site.instance->setAttr(kSiteAttr, builder.getStringAttr(site.path));
    }

    if (failed(writeInventory(inventoryPath, policy, actualTop, scopePrefix,
                              sites, module)))
      signalPassFailure();
  }
};

static void registerPasses() {
  PassRegistration<SelectHWTopPass>();
  PassRegistration<MapMemorySitesPass>();
}

} // namespace

extern "C" LLVM_ATTRIBUTE_WEAK mlir::PassPluginLibraryInfo
mlirGetPassPluginInfo() {
  return {MLIR_PLUGIN_API_VERSION, "RhodiumMemorySites", "1",
          []() { registerPasses(); }};
}
