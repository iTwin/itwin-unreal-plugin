// This file was generated by generate-classes.
// DO NOT EDIT THIS FILE!
#pragma once

#include <CesiumGltf/ExtensionKhrTextureBasisu.h>
#include <CesiumJsonReader/ExtensibleObjectJsonHandler.h>
#include <CesiumJsonReader/IntegerJsonHandler.h>

namespace CesiumJsonReader {
class JsonReaderOptions;
}

namespace CesiumGltfReader {
class ExtensionKhrTextureBasisuJsonHandler
    : public CesiumJsonReader::ExtensibleObjectJsonHandler,
      public CesiumJsonReader::IExtensionJsonHandler {
public:
  using ValueType = CesiumGltf::ExtensionKhrTextureBasisu;

  static inline constexpr const char* ExtensionName = "KHR_texture_basisu";

  ExtensionKhrTextureBasisuJsonHandler(
      const CesiumJsonReader::JsonReaderOptions& options) noexcept;
  void reset(
      IJsonHandler* pParentHandler,
      CesiumGltf::ExtensionKhrTextureBasisu* pObject);

  virtual IJsonHandler* readObjectKey(const std::string_view& str) override;

  virtual void reset(
      IJsonHandler* pParentHandler,
      CesiumUtility::ExtensibleObject& o,
      const std::string_view& extensionName) override;

  virtual IJsonHandler& getHandler() override { return *this; }

protected:
  IJsonHandler* readObjectKeyExtensionKhrTextureBasisu(
      const std::string& objectType,
      const std::string_view& str,
      CesiumGltf::ExtensionKhrTextureBasisu& o);

private:
  CesiumGltf::ExtensionKhrTextureBasisu* _pObject = nullptr;
  CesiumJsonReader::IntegerJsonHandler<int32_t> _source;
};
} // namespace CesiumGltfReader