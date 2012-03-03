/*
 * Copyright 2010-2012, The Android Open Source Project
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

#ifndef BCC_SCRIPT_H
#define BCC_SCRIPT_H

#include <bcc/bcc.h>
#include "bcc_internal.h"

#include "Compiler.h"

#include <llvm/Support/CodeGen.h>

#include <vector>
#include <string>

#include <stddef.h>

namespace llvm {
  class Module;
  class GDBJITRegistrar;
}

namespace bcc {
  class ScriptCompiled;
  class ScriptCached;
  class SourceInfo;
  struct CompilerOption;

  namespace ScriptStatus {
    enum StatusType {
      Unknown,
      Compiled,
#if USE_CACHE
      Cached,
#endif
    };
  }

  namespace ScriptObject {
    enum ObjectType {
      Unknown,
      Relocatable,
      SharedObject,
      Executable,
    };
  }

  class Script {
  private:
    int mErrorCode;

    ScriptStatus::StatusType mStatus;
    ScriptObject::ObjectType mObjectType;

    union {
      ScriptCompiled *mCompiled;
#if USE_CACHE
      ScriptCached *mCached;
#endif
    };

#if USE_CACHE
    std::string mCacheDir;
    std::string mCacheName;

    inline std::string getCachedObjectPath() const {
#if USE_OLD_JIT
      return std::string(mCacheDir + mCacheName + ".jit-image");
#elif USE_MCJIT
      std::string objPath(mCacheDir + mCacheName);

      // Append suffix depends on the object type
      switch (mObjectType) {
        case ScriptObject::Relocatable:
        case ScriptObject::Executable: {
          objPath.append(".o");
          break;
        }

        case ScriptObject::SharedObject: {
          objPath.append(".so");
          break;
        }

        default: {
          assert(false && "Unknown object type!");
        }
      }
      return objPath;
#endif
    }

    inline std::string getCacheInfoPath() const {
#if USE_OLD_JIT
      return getCachedObjectPath().append(".oBCC");
#elif USE_MCJIT
      return getCachedObjectPath().append(".info");
#endif
    }
#endif

    bool mIsContextSlotNotAvail;

    // Source List
    SourceInfo *mSourceList[2];
    // Note: mSourceList[0] (main source)
    // Note: mSourceList[1] (library source)
    // TODO(logan): Generalize this, use vector or SmallVector instead!

    // External Function List
    std::vector<char const *> mUserDefinedExternalSymbols;

    // Register Symbol Lookup Function
    BCCSymbolLookupFn mpExtSymbolLookupFn;
    void *mpExtSymbolLookupFnContext;

  public:
    Script() : mErrorCode(BCC_NO_ERROR), mStatus(ScriptStatus::Unknown),
               mObjectType(ScriptObject::Unknown), mIsContextSlotNotAvail(false),
               mpExtSymbolLookupFn(NULL), mpExtSymbolLookupFnContext(NULL) {
      Compiler::GlobalInitialization();

      mSourceList[0] = NULL;
      mSourceList[1] = NULL;
    }

    ~Script();

    int addSourceBC(size_t idx,
                    char const *resName,
                    const char *bitcode,
                    size_t bitcodeSize,
                    unsigned long flags);

    int addSourceModule(size_t idx,
                        llvm::Module *module,
                        unsigned long flags);

    int addSourceFile(size_t idx,
                      char const *path,
                      unsigned long flags);

    void markExternalSymbol(char const *name) {
      mUserDefinedExternalSymbols.push_back(name);
    }

    std::vector<char const *> const &getUserDefinedExternalSymbols() const {
      return mUserDefinedExternalSymbols;
    }

    int prepareExecutable(char const *cacheDir,
                          char const *cacheName,
                          unsigned long flags);
    int writeCache();

    /*
     * Link the given bitcodes in mSourceList to shared object (.so).
     *
     * Currently, it requires one to provide the relocatable object files with
     * given bitcodes to output a shared object.
     *
     * The usage of this function is flexible. You can have a relocatable object
     * compiled before and pass it in objPath to generate shared object. If the
     * objPath is NULL, we'll invoke prepareRelocatable() to get .o first (if
     * you haven't done that yet) and then link the output relocatable object
     * file. The latter case will have libbcc compile with USE_CACHE enabled.
     *
     * TODO: Currently, we only support to link the bitcodes in mSourceList[0].
     *
     */
    int prepareSharedObject(char const *cacheDir,
                            char const *cacheName,
                            char const *objPath,
                            char const *dsoPath,
                            unsigned long flags);

    int prepareRelocatable(char const *cacheDir,
                           char const *cacheName,
                           llvm::Reloc::Model RelocModel,
                           unsigned long flags);

    char const *getCompilerErrorMessage();

    void *lookup(const char *name);


    size_t getExportVarCount() const;

    size_t getExportFuncCount() const;

    size_t getExportForEachCount() const;

    size_t getPragmaCount() const;

    size_t getFuncCount() const;

    size_t getObjectSlotCount() const;

    void getExportVarList(size_t size, void **list);

    void getExportFuncList(size_t size, void **list);

    void getExportForEachList(size_t size, void **list);

    void getExportVarNameList(std::vector<std::string> &list);

    void getExportFuncNameList(std::vector<std::string> &list);

    void getExportForEachNameList(std::vector<std::string> &list);

    void getPragmaList(size_t size,
                       char const **keyList,
                       char const **valueList);

    void getFuncInfoList(size_t size, FuncInfo *list);

    void getObjectSlotList(size_t size, uint32_t *list);

    size_t getELFSize() const;

    const char *getELF() const;

    int registerSymbolCallback(BCCSymbolLookupFn pFn, void *pContext);

#if USE_OLD_JIT
    char *getContext();
#endif

    bool isCacheable() const;

    void setError(int error) {
      if (mErrorCode == BCC_NO_ERROR && error != BCC_NO_ERROR) {
        mErrorCode = error;
      }
    }

    int getError() {
      int result = mErrorCode;
      mErrorCode = BCC_NO_ERROR;
      return result;
    }

  private:
#if USE_CACHE
    //
    // It returns 0 if there's a cache hit.
    //
    // Side effect: it will set mCacheDir, mCacheName and mObjectType.
    int internalLoadCache(char const *cacheDir, char const *cacheName,
                          ScriptObject::ObjectType objectType, bool checkOnly);
#endif
    int internalCompile(const CompilerOption&);
  };

} // namespace bcc

#endif // BCC_SCRIPT_H
