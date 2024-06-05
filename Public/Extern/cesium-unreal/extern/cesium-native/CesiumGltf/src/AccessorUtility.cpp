#include "CesiumGltf/AccessorUtility.h"

#include "CesiumGltf/Model.h"

namespace CesiumGltf {
FeatureIdAccessorType getFeatureIdAccessorView(
    const Model& model,
    const MeshPrimitive& primitive,
    int32_t featureIdAttributeIndex) {
  const std::string attributeName =
      "_FEATURE_ID_" + std::to_string(featureIdAttributeIndex);
  auto featureId = primitive.attributes.find(attributeName);
  if (featureId == primitive.attributes.end()) {
    return FeatureIdAccessorType();
  }

  const Accessor* pAccessor =
      model.getSafe<Accessor>(&model.accessors, featureId->second);
  if (!pAccessor || pAccessor->type != Accessor::Type::SCALAR ||
      pAccessor->normalized) {
    return FeatureIdAccessorType();
  }

  switch (pAccessor->componentType) {
  case Accessor::ComponentType::BYTE:
    return AccessorView<int8_t>(model, *pAccessor);
  case Accessor::ComponentType::UNSIGNED_BYTE:
    return AccessorView<uint8_t>(model, *pAccessor);
  case Accessor::ComponentType::SHORT:
    return AccessorView<int16_t>(model, *pAccessor);
  case Accessor::ComponentType::UNSIGNED_SHORT:
    return AccessorView<uint16_t>(model, *pAccessor);
  case Accessor::ComponentType::FLOAT:
    return AccessorView<float>(model, *pAccessor);
  default:
    return FeatureIdAccessorType();
  }
}

IndexAccessorType
getIndexAccessorView(const Model& model, const MeshPrimitive& primitive) {
  if (primitive.indices < 0) {
    return IndexAccessorType();
  }

  const Accessor* pAccessor =
      model.getSafe<Accessor>(&model.accessors, primitive.indices);
  if (!pAccessor || pAccessor->type != Accessor::Type::SCALAR ||
      pAccessor->normalized) {
    return AccessorView<uint8_t>();
  }

  switch (pAccessor->componentType) {
  case Accessor::ComponentType::UNSIGNED_BYTE:
    return AccessorView<uint8_t>(model, *pAccessor);
  case Accessor::ComponentType::UNSIGNED_SHORT:
    return AccessorView<uint16_t>(model, *pAccessor);
  case Accessor::ComponentType::UNSIGNED_INT:
    return AccessorView<uint32_t>(model, *pAccessor);
  default:
    return AccessorView<uint8_t>();
  }
}

TexCoordAccessorType getTexCoordAccessorView(
    const Model& model,
    const MeshPrimitive& primitive,
    int32_t textureCoordinateSetIndex) {
  const std::string texCoordName =
      "TEXCOORD_" + std::to_string(textureCoordinateSetIndex);
  auto texCoord = primitive.attributes.find(texCoordName);
  if (texCoord == primitive.attributes.end()) {
    return TexCoordAccessorType();
  }

  const Accessor* pAccessor =
      model.getSafe<Accessor>(&model.accessors, texCoord->second);
  if (!pAccessor || pAccessor->type != Accessor::Type::VEC2) {
    return TexCoordAccessorType();
  }

  switch (pAccessor->componentType) {
  case Accessor::ComponentType::UNSIGNED_BYTE:
    if (pAccessor->normalized) {
      // Unsigned byte texcoords must be normalized.
      return AccessorView<AccessorTypes::VEC2<uint8_t>>(model, *pAccessor);
    }
    [[fallthrough]];
  case Accessor::ComponentType::UNSIGNED_SHORT:
    if (pAccessor->normalized) {
      // Unsigned short texcoords must be normalized.
      return AccessorView<AccessorTypes::VEC2<uint16_t>>(model, *pAccessor);
    }
    [[fallthrough]];
  case Accessor::ComponentType::FLOAT:
    return AccessorView<AccessorTypes::VEC2<float>>(model, *pAccessor);
  default:
    return TexCoordAccessorType();
  }
}

} // namespace CesiumGltf
