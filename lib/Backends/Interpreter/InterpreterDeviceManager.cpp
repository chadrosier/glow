/**
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "InterpreterDeviceManager.h"
#include "Interpreter.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

static llvm::cl::OptionCategory
    InterpreterBackendCat("Glow Interpreter Backend Options");
static llvm::cl::opt<unsigned> interpreterMaxMem(
    "interpreter-memory",
    llvm::cl::desc("Interpreter DeviceManager maximum memory in kilobytes"),
    llvm::cl::init(0), llvm::cl::cat(InterpreterBackendCat));

namespace glow {
namespace runtime {

DeviceManager *createInterpreterDeviceManager(const DeviceConfig &config) {
  if (interpreterMaxMem) {
    // Convert command line interpreterMaxMem to bytes from kilobytes.
    auto configNew = config;
    configNew.setDeviceMemory(uint64_t{interpreterMaxMem} * 1024);
    return new InterpreterDeviceManager(configNew);
  }
  return new InterpreterDeviceManager(config);
}

uint64_t InterpreterDeviceManager::getMaximumMemory() const {
  return maxMemoryBytes_;
}

uint64_t InterpreterDeviceManager::getAvailableMemory() const {
  return maxMemoryBytes_ - usedMemoryBytes_;
}

bool InterpreterDeviceManager::isMemoryAvailable(uint64_t estimate) const {
  return maxMemoryBytes_ >= (usedMemoryBytes_ + estimate);
}

DeviceInfo InterpreterDeviceManager::getDeviceInfo() const {
  // TODO: these may need to be tweaked depending on interpreter overheads.
  DeviceInfo info = DeviceInfo();
  info.sramCapacity = 256 * 1024 * 1024;
  info.peakCompute = 2.2 * 1024 * 1024 * 1024 * 1024;
  info.peakDramBw = 110.0 * 1024 * 1024 * 1024;
  info.peakSramBw = 1024.0 * 1024 * 1024 * 1024;
  info.peakPCIeBw = 16.0 * 1024 * 1024 * 1024;
  return info;
}

void InterpreterDeviceManager::addNetworkImpl(const Module *module,
                                              FunctionMapTy functions,
                                              ReadyCBTy readyCB) {
  DCHECK(readyCB != nullptr);

  uint64_t allFunctionsMemoryBytes{0};

  // First check for uniqueness of the function name.
  for (const auto &func : functions) {
    if (functions_.count(func.first) != 0) {
      readyCB(
          module,
          MAKE_ERR(
              llvm::formatv(
                  "Failed to add network: already have a function called {0}",
                  func.first)
                  .str()));
      return;
    }

    if (func.second->getCompileBackendName() != Interpreter::getName()) {
      readyCB(module, MAKE_ERR(llvm::formatv("Failed to add network: function "
                                             "{0} is not a InterpreterFunction",
                                             func.first)
                                   .str()));
      return;
    }

    allFunctionsMemoryBytes +=
        func.second->getRuntimeBundle().getConstantWeightSize();
  }

  if (usedMemoryBytes_ + allFunctionsMemoryBytes > maxMemoryBytes_) {
    readyCB(module, MAKE_ERR(GlowErr::ErrorCode::RUNTIME_OUT_OF_DEVICE_MEMORY,
                             "Failed to add network: not enough memory"));
    return;
  }

  // Add to the function name lookup map.
  for (const auto &func : functions) {
    if (func.second->getRuntimeBundle().getConstants() == nullptr) {
      func.second->collectConstants(module);
    }
    functions_.emplace(func.first, func.second);
  }

  usedMemoryBytes_ += allFunctionsMemoryBytes;
  assert(usedMemoryBytes_ <= maxMemoryBytes_);

  // Export changes to memory use.
  exportMemoryCounters();
  // Fire the ready CB.
  readyCB(module, llvm::Error::success());
}

void InterpreterDeviceManager::evictNetworkImpl(std::string functionName,
                                                EvictFunctionCBTy evictCB) {
  DCHECK(evictCB != nullptr);

  auto it = functions_.find(functionName);

  if (it != functions_.end()) {
    usedMemoryBytes_ -= it->second->getRuntimeBundle().getConstantWeightSize();
    functions_.erase(it);
  } else {
    evictCB(functionName,
            MAKE_ERR(GlowErr::ErrorCode::RUNTIME_NET_NOT_FOUND,
                     strFormat("Could not find function with name %s to evict",
                               functionName.c_str())));
    return;
  }
  exportMemoryCounters();
  evictCB(functionName, llvm::Error::success());
}

void InterpreterDeviceManager::runFunctionImpl(
    RunIdentifierTy id, std::string function,
    std::unique_ptr<ExecutionContext> context, ResultCBTy resultCB) {
  DCHECK(resultCB != nullptr);

  TRACE_EVENT_SCOPE_NAMED(context->getTraceContext(), TraceLevel::RUNTIME,
                          "DeviceManager::run", dmRun);
  auto funcIt = functions_.find(function);
  if (funcIt == functions_.end()) {
    dmRun.addArg("reason", "function not found");
    TRACE_EVENT_SCOPE_END_NAMED(dmRun);
    resultCB(id,
             MAKE_ERR(GlowErr::ErrorCode::RUNTIME_NET_NOT_FOUND,
                      llvm::formatv("Function {0} not found", function).str()),
             std::move(context));
    return;
  }

  CompiledFunction *func = funcIt->second;

  // Run that function.
  auto executeErr = func->execute(context.get());

  // End the TraceEvent early to avoid time in the CB.
  TRACE_EVENT_SCOPE_END_NAMED(dmRun);

  // Fire the resultCB.
  resultCB(id, std::move(executeErr), std::move(context));
}

} // namespace runtime
} // namespace glow
