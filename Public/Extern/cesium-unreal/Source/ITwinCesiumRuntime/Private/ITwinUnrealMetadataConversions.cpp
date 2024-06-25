// Copyright 2020-2023 CesiumGS, Inc. and Contributors

#include "ITwinUnrealMetadataConversions.h"

EITwinCesiumMetadataBlueprintType
CesiumMetadataValueTypeToBlueprintType(FITwinCesiumMetadataValueType ValueType) {
  if (ValueType.bIsArray) {
    return EITwinCesiumMetadataBlueprintType::Array;
  }

  EITwinCesiumMetadataType type = ValueType.Type;
  EITwinCesiumMetadataComponentType componentType = ValueType.ComponentType;

  if (type == EITwinCesiumMetadataType::Boolean) {
    return EITwinCesiumMetadataBlueprintType::Boolean;
  }

  if (type == EITwinCesiumMetadataType::String) {
    return EITwinCesiumMetadataBlueprintType::String;
  }

  if (type == EITwinCesiumMetadataType::Scalar) {
    switch (componentType) {
    case EITwinCesiumMetadataComponentType::Uint8:
      return EITwinCesiumMetadataBlueprintType::Byte;
    case EITwinCesiumMetadataComponentType::Int8:
    case EITwinCesiumMetadataComponentType::Int16:
    case EITwinCesiumMetadataComponentType::Uint16:
    case EITwinCesiumMetadataComponentType::Int32:
      return EITwinCesiumMetadataBlueprintType::Integer;
    case EITwinCesiumMetadataComponentType::Uint32:
    case EITwinCesiumMetadataComponentType::Int64:
      return EITwinCesiumMetadataBlueprintType::Integer64;
    case EITwinCesiumMetadataComponentType::Float32:
      return EITwinCesiumMetadataBlueprintType::Float;
    case EITwinCesiumMetadataComponentType::Float64:
      return EITwinCesiumMetadataBlueprintType::Float64;
    case EITwinCesiumMetadataComponentType::Uint64:
    default:
      return EITwinCesiumMetadataBlueprintType::String;
    }
  }

  if (type == EITwinCesiumMetadataType::Vec2) {
    switch (componentType) {
    case EITwinCesiumMetadataComponentType::Uint8:
    case EITwinCesiumMetadataComponentType::Int8:
    case EITwinCesiumMetadataComponentType::Int16:
    case EITwinCesiumMetadataComponentType::Uint16:
    case EITwinCesiumMetadataComponentType::Int32:
      return EITwinCesiumMetadataBlueprintType::IntPoint;
    default:
      return EITwinCesiumMetadataBlueprintType::Vector2D;
    }
  }

  if (type == EITwinCesiumMetadataType::Vec3) {
    switch (componentType) {
    case EITwinCesiumMetadataComponentType::Uint8:
    case EITwinCesiumMetadataComponentType::Int8:
    case EITwinCesiumMetadataComponentType::Int16:
    case EITwinCesiumMetadataComponentType::Uint16:
    case EITwinCesiumMetadataComponentType::Int32:
      return EITwinCesiumMetadataBlueprintType::IntVector;
    case EITwinCesiumMetadataComponentType::Float32:
      return EITwinCesiumMetadataBlueprintType::Vector3f;
    default:
      return EITwinCesiumMetadataBlueprintType::Vector3;
    }
  }

  if (type == EITwinCesiumMetadataType::Vec4) {
    return EITwinCesiumMetadataBlueprintType::Vector4;
  }

  if (type == EITwinCesiumMetadataType::Mat2 || type == EITwinCesiumMetadataType::Mat3 ||
      type == EITwinCesiumMetadataType::Mat4) {
    return EITwinCesiumMetadataBlueprintType::Matrix;
  }

  return EITwinCesiumMetadataBlueprintType::None;
}

EITwinCesiumMetadataBlueprintType CesiumMetadataTrueTypeToBlueprintType(
    EITwinCesiumMetadataTrueType_DEPRECATED trueType) {
  switch (trueType) {
  case EITwinCesiumMetadataTrueType_DEPRECATED::Boolean_DEPRECATED:
    return EITwinCesiumMetadataBlueprintType::Boolean;
  case EITwinCesiumMetadataTrueType_DEPRECATED::Uint8_DEPRECATED:
    return EITwinCesiumMetadataBlueprintType::Byte;
  case EITwinCesiumMetadataTrueType_DEPRECATED::Int8_DEPRECATED:
  case EITwinCesiumMetadataTrueType_DEPRECATED::Int16_DEPRECATED:
  case EITwinCesiumMetadataTrueType_DEPRECATED::Uint16_DEPRECATED:
  case EITwinCesiumMetadataTrueType_DEPRECATED::Int32_DEPRECATED:
  // TODO: remove this one
  case EITwinCesiumMetadataTrueType_DEPRECATED::Uint32_DEPRECATED:
    return EITwinCesiumMetadataBlueprintType::Integer;
  case EITwinCesiumMetadataTrueType_DEPRECATED::Int64_DEPRECATED:
    return EITwinCesiumMetadataBlueprintType::Integer64;
  case EITwinCesiumMetadataTrueType_DEPRECATED::Float32_DEPRECATED:
    return EITwinCesiumMetadataBlueprintType::Float;
  case EITwinCesiumMetadataTrueType_DEPRECATED::Float64_DEPRECATED:
    return EITwinCesiumMetadataBlueprintType::Float64;
  case EITwinCesiumMetadataTrueType_DEPRECATED::Uint64_DEPRECATED:
  case EITwinCesiumMetadataTrueType_DEPRECATED::String_DEPRECATED:
    return EITwinCesiumMetadataBlueprintType::String;
  case EITwinCesiumMetadataTrueType_DEPRECATED::Array_DEPRECATED:
    return EITwinCesiumMetadataBlueprintType::Array;
  default:
    return EITwinCesiumMetadataBlueprintType::None;
  }
}

EITwinCesiumMetadataTrueType_DEPRECATED
CesiumMetadataValueTypeToTrueType(FITwinCesiumMetadataValueType ValueType) {
  if (ValueType.bIsArray) {
    return EITwinCesiumMetadataTrueType_DEPRECATED::Array_DEPRECATED;
  }

  CesiumGltf::PropertyType type = CesiumGltf::PropertyType(ValueType.Type);
  CesiumGltf::PropertyComponentType componentType =
      CesiumGltf::PropertyComponentType(ValueType.ComponentType);

  if (type == CesiumGltf::PropertyType::Boolean) {
    return EITwinCesiumMetadataTrueType_DEPRECATED::Boolean_DEPRECATED;
  }

  if (type == CesiumGltf::PropertyType::Scalar) {
    switch (componentType) {
    case CesiumGltf::PropertyComponentType::Uint8:
      return EITwinCesiumMetadataTrueType_DEPRECATED::Uint8_DEPRECATED;
    case CesiumGltf::PropertyComponentType::Int8:
      return EITwinCesiumMetadataTrueType_DEPRECATED::Int8_DEPRECATED;
    case CesiumGltf::PropertyComponentType::Uint16:
      return EITwinCesiumMetadataTrueType_DEPRECATED::Uint16_DEPRECATED;
    case CesiumGltf::PropertyComponentType::Int16:
      return EITwinCesiumMetadataTrueType_DEPRECATED::Int16_DEPRECATED;
    case CesiumGltf::PropertyComponentType::Uint32:
      return EITwinCesiumMetadataTrueType_DEPRECATED::Uint32_DEPRECATED;
    case CesiumGltf::PropertyComponentType::Int32:
      return EITwinCesiumMetadataTrueType_DEPRECATED::Int32_DEPRECATED;
    case CesiumGltf::PropertyComponentType::Int64:
      return EITwinCesiumMetadataTrueType_DEPRECATED::Int64_DEPRECATED;
    case CesiumGltf::PropertyComponentType::Uint64:
      return EITwinCesiumMetadataTrueType_DEPRECATED::Uint64_DEPRECATED;
    case CesiumGltf::PropertyComponentType::Float32:
      return EITwinCesiumMetadataTrueType_DEPRECATED::Float32_DEPRECATED;
    case CesiumGltf::PropertyComponentType::Float64:
      return EITwinCesiumMetadataTrueType_DEPRECATED::Float64_DEPRECATED;
    default:
      return EITwinCesiumMetadataTrueType_DEPRECATED::None_DEPRECATED;
    }
  }

  if (type == CesiumGltf::PropertyType::String) {
    return EITwinCesiumMetadataTrueType_DEPRECATED::String_DEPRECATED;
  }

  return EITwinCesiumMetadataTrueType_DEPRECATED::None_DEPRECATED;
}

/*static*/ FIntPoint
FITwinUnrealMetadataConversions::toIntPoint(const glm::ivec2& vec2) {
  return FIntPoint(vec2[0], vec2[1]);
}

/*static*/ FIntPoint FITwinUnrealMetadataConversions::toIntPoint(
    const std::string_view& string,
    const FIntPoint& defaultValue) {
  FString unrealString = FITwinUnrealMetadataConversions::toString(string);

  // For some reason, FIntPoint doesn't have an InitFromString method, so
  // copy the one from FVector.
  int32 X = 0, Y = 0;
  const bool bSuccessful = FParse::Value(*unrealString, TEXT("X="), X) &&
                           FParse::Value(*unrealString, TEXT("Y="), Y);
  return bSuccessful ? FIntPoint(X, Y) : defaultValue;
}

/*static*/ FVector2D
FITwinUnrealMetadataConversions::toVector2D(const glm::dvec2& vec2) {
  return FVector2D(vec2[0], vec2[1]);
}

/*static*/ FVector2D FITwinUnrealMetadataConversions::toVector2D(
    const std::string_view& string,
    const FVector2D& defaultValue) {
  FString unrealString = FITwinUnrealMetadataConversions::toString(string);
  FVector2D result;
  return result.InitFromString(unrealString) ? result : defaultValue;
}

/*static*/ FIntVector
FITwinUnrealMetadataConversions::toIntVector(const glm::ivec3& vec3) {
  return FIntVector(vec3[0], vec3[1], vec3[2]);
}

/*static*/ FIntVector FITwinUnrealMetadataConversions::toIntVector(
    const std::string_view& string,
    const FIntVector& defaultValue) {
  FString unrealString = FITwinUnrealMetadataConversions::toString(string);
  // For some reason, FIntVector doesn't have an InitFromString method, so
  // copy the one from FVector.
  int32 X = 0, Y = 0, Z = 0;
  const bool bSuccessful = FParse::Value(*unrealString, TEXT("X="), X) &&
                           FParse::Value(*unrealString, TEXT("Y="), Y) &&
                           FParse::Value(*unrealString, TEXT("Z="), Z);
  return bSuccessful ? FIntVector(X, Y, Z) : defaultValue;
}

/*static*/ FVector3f
FITwinUnrealMetadataConversions::toVector3f(const glm::vec3& vec3) {
  return FVector3f(vec3[0], vec3[1], vec3[2]);
}

/*static*/ FVector3f FITwinUnrealMetadataConversions::toVector3f(
    const std::string_view& string,
    const FVector3f& defaultValue) {
  FString unrealString = FITwinUnrealMetadataConversions::toString(string);
  FVector3f result;
  return result.InitFromString(unrealString) ? result : defaultValue;
}

/*static*/ FVector FITwinUnrealMetadataConversions::toVector(const glm::dvec3& vec3) {
  return FVector(vec3[0], vec3[1], vec3[2]);
}

/*static*/ FVector FITwinUnrealMetadataConversions::toVector(
    const std::string_view& string,
    const FVector& defaultValue) {
  FString unrealString = FITwinUnrealMetadataConversions::toString(string);
  FVector result;
  return result.InitFromString(unrealString) ? result : defaultValue;
}

/*static*/ FVector4
FITwinUnrealMetadataConversions::toVector4(const glm::dvec4& vec4) {
  return FVector4(vec4[0], vec4[1], vec4[2], vec4[3]);
}

/*static*/ FVector4 FITwinUnrealMetadataConversions::toVector4(
    const std::string_view& string,
    const FVector4& defaultValue) {
  FString unrealString = FITwinUnrealMetadataConversions::toString(string);
  FVector4 result;
  return result.InitFromString(unrealString) ? result : defaultValue;
}

/*static*/ FMatrix FITwinUnrealMetadataConversions::toMatrix(const glm::dmat4& mat4) {
  // glm is column major, but Unreal is row major.
  FPlane4d row1(
      static_cast<double>(mat4[0][0]),
      static_cast<double>(mat4[1][0]),
      static_cast<double>(mat4[2][0]),
      static_cast<double>(mat4[3][0]));

  FPlane4d row2(
      static_cast<double>(mat4[0][1]),
      static_cast<double>(mat4[1][1]),
      static_cast<double>(mat4[2][1]),
      static_cast<double>(mat4[3][1]));

  FPlane4d row3(
      static_cast<double>(mat4[0][2]),
      static_cast<double>(mat4[1][2]),
      static_cast<double>(mat4[2][2]),
      static_cast<double>(mat4[3][2]));

  FPlane4d row4(
      static_cast<double>(mat4[0][3]),
      static_cast<double>(mat4[1][3]),
      static_cast<double>(mat4[2][3]),
      static_cast<double>(mat4[3][3]));

  return FMatrix(row1, row2, row3, row4);
}

/*static*/ FString
FITwinUnrealMetadataConversions::toString(const std::string_view& from) {
  return toString(std::string(from));
}

/*static*/ FString
FITwinUnrealMetadataConversions::toString(const std::string& from) {
  return FString(UTF8_TO_TCHAR(from.data()));
}
