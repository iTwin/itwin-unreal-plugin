// This file was generated by generate-classes.
// DO NOT EDIT THIS FILE!
#pragma once

#include "ExtensionExtFeatureMetadataFeatureTableJsonHandler.h"
#include "ExtensionExtFeatureMetadataFeatureTextureJsonHandler.h"
#include "ExtensionExtFeatureMetadataSchemaJsonHandler.h"
#include "ExtensionExtFeatureMetadataStatisticsJsonHandler.h"

#include <CesiumGltf/ExtensionModelExtFeatureMetadata.h>
#include <CesiumJsonReader/DictionaryJsonHandler.h>
#include <CesiumJsonReader/ExtensibleObjectJsonHandler.h>
#include <CesiumJsonReader/StringJsonHandler.h>

namespace CesiumJsonReader {
class JsonReaderOptions;
}

namespace CesiumGltfReader {
class ExtensionModelExtFeatureMetadataJsonHandler
    : public CesiumJsonReader::ExtensibleObjectJsonHandler,
      public CesiumJsonReader::IExtensionJsonHandler {
public:
  using ValueType = CesiumGltf::ExtensionModelExtFeatureMetadata;

  static inline constexpr const char* ExtensionName = "EXT_feature_metadata";

  ExtensionModelExtFeatureMetadataJsonHandler(
      const CesiumJsonReader::JsonReaderOptions& options) noexcept;
  void reset(
      IJsonHandler* pParentHandler,
      CesiumGltf::ExtensionModelExtFeatureMetadata* pObject);

  virtual IJsonHandler* readObjectKey(const std::string_view& str) override;

  virtual void reset(
      IJsonHandler* pParentHandler,
      CesiumUtility::ExtensibleObject& o,
      const std::string_view& extensionName) override;

  virtual IJsonHandler& getHandler() override { return *this; }

protected:
  IJsonHandler* readObjectKeyExtensionModelExtFeatureMetadata(
      const std::string& objectType,
      const std::string_view& str,
      CesiumGltf::ExtensionModelExtFeatureMetadata& o);

private:
  CesiumGltf::ExtensionModelExtFeatureMetadata* _pObject = nullptr;
  ExtensionExtFeatureMetadataSchemaJsonHandler _schema;
  CesiumJsonReader::StringJsonHandler _schemaUri;
  ExtensionExtFeatureMetadataStatisticsJsonHandler _statistics;
  CesiumJsonReader::DictionaryJsonHandler<
      CesiumGltf::ExtensionExtFeatureMetadataFeatureTable,
      ExtensionExtFeatureMetadataFeatureTableJsonHandler>
      _featureTables;
  CesiumJsonReader::DictionaryJsonHandler<
      CesiumGltf::ExtensionExtFeatureMetadataFeatureTexture,
      ExtensionExtFeatureMetadataFeatureTextureJsonHandler>
      _featureTextures;
};
} // namespace CesiumGltfReader