/*--------------------------------------------------------------------------------------+
|
|     $Source: TestGltfTuner.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <catch2/catch.hpp>
#include <BeUtils/Gltf/GltfTuner.h>
#include <BeUtils/Gltf/GltfBuilder.h>
#include <CesiumGltf/ExtensionModelExtStructuralMetadata.h>
#include <CesiumGltf/ExtensionExtMeshFeatures.h>
#include <CesiumGltf/ExtensionITwinMaterialID.h>
#include <CesiumGltfWriter/GltfWriter.h>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>

struct Vertex
{
	int vertexId_ = -1;
	float featureId_ = -1.f;
	uint16_t featureId1_ = 0; // second feature slot, used for iTwin material IDs

	template <std::size_t FeatIndex = 0>
	auto GetFeature() const
	{
		return std::get<FeatIndex>(std::make_tuple(featureId_, featureId1_));
	}

};
using Patch = std::vector<Vertex>;

static constexpr int32_t g_ComponentTypeAuto = -1;
static constexpr int32_t g_ComponentTypeNoData = -2;

struct DataFormat
{
	int32_t componentType = g_ComponentTypeAuto;
	std::string type;
};

struct AddMeshPrimitiveArgs
{
	BeUtils::GltfBuilder& gltfBuilder;
	std::vector<Patch> patches;
	std::optional<std::vector<std::array<uint8_t, 1>>> indices;
	int material = -1;
	int mode = CesiumGltf::MeshPrimitive::Mode::TRIANGLES;
	DataFormat indexFormat;
	DataFormat normalFormat;
	DataFormat uvFormat;
	DataFormat colorFormat;
	DataFormat featureIdFormat;
	DataFormat materialFeatureIdFormat = { g_ComponentTypeNoData }; // for iTwin material IDs
	std::optional<uint64_t> iTwinMaterialId;
};


template <std::size_t FeatIndex = 0>
void FillPrimitiveFeatureIds(BeUtils::GltfBuilder::MeshPrimitive& primitive,
	const DataFormat& featureIdFormat, const AddMeshPrimitiveArgs& args)
{
	const auto setFeatureIds = [&](auto* unusedComponentType)
	{
		using ComponentType = std::decay_t<decltype(*unusedComponentType)>;
		std::vector<std::array<ComponentType, 1>> featureIds;
		for (const auto& patch : args.patches)
			for (const auto& vertex : patch)
				featureIds.push_back({ (ComponentType)vertex.GetFeature<FeatIndex>() });
		primitive.SetFeatureIds(featureIds, args.materialFeatureIdFormat.componentType != g_ComponentTypeNoData);
	};
	switch (featureIdFormat.componentType)
	{
	case CesiumGltf::Accessor::ComponentType::BYTE:
		setFeatureIds((int8_t*)0);
		break;
	case CesiumGltf::Accessor::ComponentType::UNSIGNED_BYTE:
		setFeatureIds((uint8_t*)0);
		break;
	case CesiumGltf::Accessor::ComponentType::SHORT:
		setFeatureIds((int16_t*)0);
		break;
	case CesiumGltf::Accessor::ComponentType::UNSIGNED_SHORT:
		setFeatureIds((uint16_t*)0);
		break;
	case CesiumGltf::Accessor::ComponentType::UNSIGNED_INT:
		setFeatureIds((uint32_t*)0);
		break;
	case g_ComponentTypeAuto:
	case CesiumGltf::Accessor::ComponentType::FLOAT:
		setFeatureIds((float*)0);
		break;
	default:
		assert(false);
		break;
	}
}

void AddMeshPrimitive(const AddMeshPrimitiveArgs& args)
{
	auto primitive = args.gltfBuilder.AddMeshPrimitive(0, args.material, args.mode);
	if (args.indices)
		primitive.SetIndices(*args.indices, false);
	else
	{
		const auto setIndices = [&](auto* unusedComponentType)
			{
				using ComponentType = std::decay_t<decltype(*unusedComponentType)>;
				std::vector<std::array<ComponentType, 1>> indices;
				int patchIndex0 = 0;
				for (const auto& patch: args.patches)
				{
					switch (args.mode)
					{
						case CesiumGltf::MeshPrimitive::Mode::POINTS:
						case CesiumGltf::MeshPrimitive::Mode::LINE_LOOP:
						case CesiumGltf::MeshPrimitive::Mode::LINE_STRIP:
						case CesiumGltf::MeshPrimitive::Mode::TRIANGLE_STRIP:
						case CesiumGltf::MeshPrimitive::Mode::TRIANGLE_FAN:
							assert (args.patches.size() == 1);
							for (int i = 0; i < (int)patch.size(); ++i)
								indices.push_back({ComponentType(patchIndex0+i)});
							break;
						// For "list" modes, we create a strip,
						// so that each vertex is referenced by more than just one index.
						case CesiumGltf::MeshPrimitive::Mode::LINES:
							for (int i = 0; i < (int)patch.size()-1; ++i)
								for (int j = 0; j < 2; ++j)
									indices.push_back({ComponentType(patchIndex0+i+j)});
							break;
						case CesiumGltf::MeshPrimitive::Mode::TRIANGLES:
							for (int i = 0; i < (int)patch.size()-2; ++i)
								for (int j = 0; j < 3; ++j)
									indices.push_back({ComponentType(patchIndex0+i+j)});
							break;
					}
					patchIndex0 += (int)patch.size();
				}
				primitive.SetIndices(indices, false);
			};
		switch (args.indexFormat.componentType)
		{
			case g_ComponentTypeAuto:
			case CesiumGltf::Accessor::ComponentType::UNSIGNED_BYTE:
				setIndices((uint8_t*)0);
				break;
			case CesiumGltf::Accessor::ComponentType::UNSIGNED_SHORT:
				setIndices((uint16_t*)0);
				break;
			case CesiumGltf::Accessor::ComponentType::UNSIGNED_INT:
				setIndices((uint32_t*)0);
				break;
			default:
				assert(false);
				break;
		}
	}
	std::vector<std::array<float, 3>> positions;
	for (const auto& patch: args.patches)
		for (const auto& vertex: patch)
			positions.push_back({float(vertex.vertexId_*100), float(vertex.vertexId_*100+1), float(vertex.vertexId_*100+2)});
	primitive.SetPositions(positions);
	if (args.normalFormat.componentType != g_ComponentTypeNoData)
	{
		std::vector<std::array<float, 3>> normals;
		for (const auto& patch: args.patches)
			for (const auto& vertex: patch)
				normals.push_back({float(vertex.vertexId_*100+10), float(vertex.vertexId_*100+11), float(vertex.vertexId_*100+12)});
		primitive.SetNormals(normals);
	}
	if (args.uvFormat.componentType != g_ComponentTypeNoData)
	{
		std::vector<std::array<float, 2>> uvs;
		for (const auto& patch: args.patches)
			for (const auto& vertex: patch)
				uvs.push_back({float(vertex.vertexId_*100+20), float(vertex.vertexId_*100+21)});
		primitive.SetUVs(uvs);
	}
	if (args.colorFormat.componentType != g_ComponentTypeNoData)
	{
		const auto setColors1 = [&](auto* unusedComponentType)
			{
				using ComponentType = std::decay_t<decltype(*unusedComponentType)>;
				const auto setColor2 = [&](auto* unusedCompleteType)
					{
						using CompleteType = std::decay_t<decltype(*unusedCompleteType)>;
						std::vector<CompleteType> colors;
						for (const auto& patch: args.patches)
							for (const auto& vertex: patch)
							{
								const auto color = [&]
									{
										std::array<uint8_t, std::tuple_size_v<CompleteType>> color;
										for (int c = 0; c < (int)color.size(); ++c)
											color[c] = uint8_t(vertex.vertexId_+10+c);
										if constexpr (std::is_same_v<uint8_t, typename CompleteType::value_type>)
											return color;
										else if constexpr (std::is_same_v<uint16_t, typename CompleteType::value_type>)
										{
											CompleteType color2;
											const auto f = [](const auto x){return uint16_t(x<<8);};
											for (int c = 0; c < (int)color.size(); ++c)
												color2[c] = f(color[c]);
											return color2;
										}
										else if constexpr (std::is_same_v<float, typename CompleteType::value_type>)
										{
											CompleteType color2;
											const auto f = [](const auto x){return (float(x)+0.5)/256.f;};
											for (int c = 0; c < (int)color.size(); ++c)
												color2[c] = f(color[c]);
											return color2;
										}
										else
											static_assert(std::is_void_v<typename CompleteType::value_type>);
									}();
								colors.push_back(color);
							}
						primitive.SetColors(colors);
					};
				if (args.colorFormat.type == "" || args.colorFormat.type == CesiumGltf::Accessor::Type::VEC4)
					setColor2((std::array<ComponentType, 4>*)0);
				else if (args.colorFormat.type == CesiumGltf::Accessor::Type::VEC3)
					setColor2((std::array<ComponentType, 3>*)0);
				else
					assert(false);
			};
		switch (args.colorFormat.componentType)
		{
			case g_ComponentTypeAuto:
			case CesiumGltf::Accessor::ComponentType::UNSIGNED_BYTE:
				setColors1((uint8_t*)0);
				break;
			case CesiumGltf::Accessor::ComponentType::UNSIGNED_SHORT:
				setColors1((uint16_t*)0);
				break;
			case CesiumGltf::Accessor::ComponentType::FLOAT:
				setColors1((float*)0);
				break;
			default:
				assert(false);
				break;
		}
	}
	if (args.featureIdFormat.componentType != g_ComponentTypeNoData)
	{
		FillPrimitiveFeatureIds<0>(primitive, args.featureIdFormat, args);
	}
	//if (args.materialFeatureIdFormat.componentType != g_ComponentTypeNoData)
	//{
	//	FillPrimitiveFeatureIds<1>(primitive, args.materialFeatureIdFormat, args);
	//}
	if (args.iTwinMaterialId)
	{
		primitive.SetITwinMaterialID(*args.iTwinMaterialId);
	}
}
#ifdef _WIN32
#define _TEXT(quote) L##quote
#define _TOSTRING(s) std::to_wstring(s)

#else
#define _TEXT(quote) #quote
#define _TOSTRING(s) std::to_string(s)
#endif

//! Recursively sorts the content of the objects contained in this value.
void SortJson(rapidjson::Value& value)
{
	switch (value.GetType())
	{
		case rapidjson::kObjectType:
			// Do a bubble sort. Not the fastest sorting method, but easy to implement using rapidjson api.
			for (auto it = value.MemberBegin(); it != value.MemberEnd(); ++it)
			{
				rapidjson::Value::Member* lowerMember = &*it;
				for (auto it2 = it; it2 != value.MemberEnd(); ++it2)
					if (strcmp(it2->name.GetString(), lowerMember->name.GetString()) < 0)
						lowerMember = &*it2;
				std::swap(*it, *lowerMember);
				SortJson(it->value);
			}
			break;
		case rapidjson::kArrayType:
			for (auto it = value.Begin(); it != value.End(); ++it)
				SortJson(*it);
			break;
	}
}

namespace
{
	// CesiumGltf::ExtensionITwinMaterialID is just used internally by ITwin plugin,
	// and is missing an official writer in cesium-native...
	// Add support for writing here
	struct ExtensionITwinMaterialIDJsonWriter
	{
		using ValueType = CesiumGltf::ExtensionITwinMaterialID;

		static inline constexpr const char* ExtensionName = "ITWIN_material_identifier";

		static void write(
			const CesiumGltf::ExtensionITwinMaterialID& obj,
			CesiumJsonWriter::JsonWriter& jsonWriter,
			const CesiumJsonWriter::ExtensionWriterContext& context);
	};

	/*static*/ void ExtensionITwinMaterialIDJsonWriter::write(
		const CesiumGltf::ExtensionITwinMaterialID& obj,
		CesiumJsonWriter::JsonWriter& jsonWriter,
		const CesiumJsonWriter::ExtensionWriterContext& /*context*/)
	{
		jsonWriter.StartObject();

		jsonWriter.Key("materialId");
		jsonWriter.Uint64(obj.materialId);

		//writeExtensibleObject(obj, jsonWriter, context);

		jsonWriter.EndObject();
	}

	class ITwinGltfWriter : public CesiumGltfWriter::GltfWriter
	{
		using Super = CesiumGltfWriter::GltfWriter;
	public:
		ITwinGltfWriter() : Super()
		{
			getExtensions().registerExtension<
				CesiumGltf::MeshPrimitive,
				ExtensionITwinMaterialIDJsonWriter>();
		}
	};

	// Name of the default feature table produced by the Mesh-Export Service
	// this constant could be anything else (used to be "MeshPart"...)
	static const std::string FEATURE_TABLE_NAME = "features";

	static const std::string MATID_TABLE_NAME = "materials";
}

void CheckGltf(const CesiumGltf::Model& expected, const CesiumGltf::Model& actual)
{
	// Save models on disk so that when test fails it's easy to spot the difference
	// by using a tool (eg. WinMerge) to compare the "expected" and "actual" folders.
	const auto writeGltf = [&](const CesiumGltf::Model& model, const std::string subDir)
		{
			const auto pathBase = std::filesystem::path(BEUTILS_WORK_DIR)/subDir/Catch::getResultCapture().getCurrentTestName();
			std::filesystem::create_directories(pathBase.parent_path());
			const auto jsonBytes = ITwinGltfWriter().writeGltf(model, {.prettyPrint = true}).gltfBytes;
			// The json generated by the GltfWriter is not guaranteed to be reproducible (json object members are not sorted).
			// Since we are going to compare the serialized json contents, we need to sort the json first.
			rapidjson::Document sortedJsonDoc;
			sortedJsonDoc.Parse((const char*)jsonBytes.data(), jsonBytes.size());
			SortJson(sortedJsonDoc);
			rapidjson::StringBuffer sortedJsonBuffer;
			rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(sortedJsonBuffer);
			sortedJsonDoc.Accept(writer);
			std::ofstream(pathBase.native() + _TEXT(".json")) << sortedJsonBuffer.GetString() << std::endl;
			for (auto bufferIndex = 0; bufferIndex < model.buffers.size(); ++bufferIndex)
				std::ofstream(pathBase.native()+ _TOSTRING(bufferIndex)+ _TEXT(".bin")) <<
					std::string((const char*)model.buffers[bufferIndex].cesium.data.data(),
					model.buffers[bufferIndex].cesium.data.size());
			return std::string(sortedJsonBuffer.GetString());
		};
	// Compare a boolean (result of a lambda) instead of the buffers directly,
	// to avoid having a dump of the entire buffers in stdout when the test fails.
	REQUIRE([&]{return writeGltf(expected, "expected") == writeGltf(actual, "actual");}());
	REQUIRE(expected.buffers.size() == actual.buffers.size());
	for (int bufferIndex = 0; bufferIndex < (int)expected.buffers.size(); ++bufferIndex)
		REQUIRE([&]{return expected.buffers[bufferIndex].cesium.data == actual.buffers[bufferIndex].cesium.data;}());
}

TEST_CASE("TestNoFeatureId")
{
	BeUtils::GltfBuilder gltfBuilder;
	gltfBuilder.GetModel().meshes.emplace_back();
	int v = 0;
	AddMeshPrimitive({.gltfBuilder = gltfBuilder,
		.patches = {{{v++},{v++},{v++}}, {{v++},{v++},{v++}}},
		.featureIdFormat = {g_ComponentTypeNoData}});
	CheckGltf(gltfBuilder.GetModel(), BeUtils::GltfTuner().Tune(gltfBuilder.GetModel()));
}

TEST_CASE("TestSinglePrimitive")
{
	BeUtils::GltfBuilder gltfBuilder;
	gltfBuilder.AddMetadataProperty(FEATURE_TABLE_NAME, "element", std::vector<uint64_t>{100, 101, 102});
	// Add some unused properties (in lexicographic order), to verify that the tuner
	// also sorts the properties (if not, CheckGltf will fail).
	gltfBuilder.AddMetadataProperty(FEATURE_TABLE_NAME, "model", std::vector<uint64_t>{200, 201, 202});
	gltfBuilder.AddMetadataProperty(FEATURE_TABLE_NAME, "subcategory", std::vector<uint64_t>{300, 301, 302});
	gltfBuilder.GetModel().meshes.emplace_back();
	int v = 0;
	AddMeshPrimitive({.gltfBuilder = gltfBuilder,
		.patches = {{{v++,0},{v++,0},{v++,0}}, {{v++,1},{v++,1},{v++,1}}}});
	CheckGltf(gltfBuilder.GetModel(), BeUtils::GltfTuner().Tune(gltfBuilder.GetModel()));
}

TEST_CASE("TestConversionLineLoop")
{
	BeUtils::GltfBuilder gltfBuilder;
	gltfBuilder.AddMetadataProperty(FEATURE_TABLE_NAME, "element", std::vector<uint64_t>{100,101});
	gltfBuilder.GetModel().meshes.emplace_back();
	int v = 0;
	AddMeshPrimitive({.gltfBuilder = gltfBuilder,
		.patches = {{{v++,0},{v++,1},{v++,0}}},
		.mode = CesiumGltf::MeshPrimitive::Mode::LINE_LOOP});
	BeUtils::GltfBuilder expectedBuilder;
	expectedBuilder.GetModel().meshes.emplace_back();
	expectedBuilder.AddMetadataProperty(FEATURE_TABLE_NAME, "element", std::vector<uint64_t>{100,101});
	AddMeshPrimitive({.gltfBuilder = expectedBuilder,
		.patches = {{{0,0},{1,1},{2,0}}},
		.indices = {{{0},{1},{1},{2},{2},{0}}},
		.mode = CesiumGltf::MeshPrimitive::Mode::LINES});
	CheckGltf(expectedBuilder.GetModel(), BeUtils::GltfTuner().Tune(gltfBuilder.GetModel()));
}

TEST_CASE("TestConversionLineStrip")
{
	BeUtils::GltfBuilder gltfBuilder;
	gltfBuilder.AddMetadataProperty(FEATURE_TABLE_NAME, "element", std::vector<uint64_t>{100,101});
	gltfBuilder.GetModel().meshes.emplace_back();
	int v = 0;
	AddMeshPrimitive({.gltfBuilder = gltfBuilder,
		.patches = {{{v++,0},{v++,1},{v++,0}}},
		.mode = CesiumGltf::MeshPrimitive::Mode::LINE_STRIP});
	BeUtils::GltfBuilder expectedBuilder;
	expectedBuilder.GetModel().meshes.emplace_back();
	expectedBuilder.AddMetadataProperty(FEATURE_TABLE_NAME, "element", std::vector<uint64_t>{100,101});
	AddMeshPrimitive({.gltfBuilder = expectedBuilder,
		.patches = {{{0,0},{1,1},{2,0}}},
		.indices = {{{0},{1},{1},{2}}},
		.mode = CesiumGltf::MeshPrimitive::Mode::LINES});
	CheckGltf(expectedBuilder.GetModel(), BeUtils::GltfTuner().Tune(gltfBuilder.GetModel()));
}

TEST_CASE("TestConversionTriangleStrip")
{
	BeUtils::GltfBuilder gltfBuilder;
	gltfBuilder.AddMetadataProperty(FEATURE_TABLE_NAME, "element", std::vector<uint64_t>{100,101});
	gltfBuilder.GetModel().meshes.emplace_back();
	int v = 0;
	AddMeshPrimitive({.gltfBuilder = gltfBuilder,
		.patches = {{{v++,0},{v++,1},{v++,0},{v++,1}}},
		.mode = CesiumGltf::MeshPrimitive::Mode::TRIANGLE_STRIP});
	BeUtils::GltfBuilder expectedBuilder;
	expectedBuilder.GetModel().meshes.emplace_back();
	expectedBuilder.AddMetadataProperty(FEATURE_TABLE_NAME, "element", std::vector<uint64_t>{100,101});
	AddMeshPrimitive({.gltfBuilder = expectedBuilder,
		.patches = {{{0,0},{1,1},{2,0},{3,1}}},
		.indices = {{{0},{1},{2},{1},{3},{2}}},
		.mode = CesiumGltf::MeshPrimitive::Mode::TRIANGLES});
	CheckGltf(expectedBuilder.GetModel(), BeUtils::GltfTuner().Tune(gltfBuilder.GetModel()));
}

TEST_CASE("TestConversionTriangleFan")
{
	BeUtils::GltfBuilder gltfBuilder;
	gltfBuilder.AddMetadataProperty(FEATURE_TABLE_NAME, "element", std::vector<uint64_t>{100,101});
	gltfBuilder.GetModel().meshes.emplace_back();
	int v = 0;
	AddMeshPrimitive({.gltfBuilder = gltfBuilder,
		.patches = {{{v++,0},{v++,1},{v++,0},{v++,1}}},
		.mode = CesiumGltf::MeshPrimitive::Mode::TRIANGLE_FAN});
	BeUtils::GltfBuilder expectedBuilder;
	expectedBuilder.GetModel().meshes.emplace_back();
	expectedBuilder.AddMetadataProperty(FEATURE_TABLE_NAME, "element", std::vector<uint64_t>{100,101});
	AddMeshPrimitive({.gltfBuilder = expectedBuilder,
		.patches = {{{1,1},{2,0},{0,0},{3,1}}},
		.indices = {{{0},{1},{2},{1},{3},{2}}},
		.mode = CesiumGltf::MeshPrimitive::Mode::TRIANGLES});
	CheckGltf(expectedBuilder.GetModel(), BeUtils::GltfTuner().Tune(gltfBuilder.GetModel()));
}

TEST_CASE("TestMerge")
{
	BeUtils::GltfBuilder gltfBuilder;
	gltfBuilder.AddMetadataProperty(FEATURE_TABLE_NAME, "element", std::vector<uint64_t>{100,101,102});
	gltfBuilder.GetModel().meshes.emplace_back();
	int v = 0;
	AddMeshPrimitive({.gltfBuilder = gltfBuilder,
		.patches = {{{v++,0},{v++,0},{v++,0}}, {{v++,1},{v++,1},{v++,1}}}});
	AddMeshPrimitive({.gltfBuilder = gltfBuilder,
		.patches = {{{v++,0},{v++,0},{v++,0}}, {{v++,2},{v++,2},{v++,2}}}});
	BeUtils::GltfBuilder expectedBuilder;
	expectedBuilder.AddMetadataProperty(FEATURE_TABLE_NAME, "element", std::vector<uint64_t>{100,101,102});
	expectedBuilder.GetModel().meshes.emplace_back();
	AddMeshPrimitive({.gltfBuilder = expectedBuilder,
		.patches = {{{0,0},{1,0},{2,0}}, {{3,1},{4,1},{5,1}}, {{6,0},{7,0},{8,0}}, {{9,2},{10,2},{11,2}}}});
	CheckGltf(expectedBuilder.GetModel(), BeUtils::GltfTuner().Tune(gltfBuilder.GetModel()));
}

TEST_CASE("TestNoMergeDifferentMaterial")
{
	BeUtils::GltfBuilder gltfBuilder;
	gltfBuilder.AddMetadataProperty(FEATURE_TABLE_NAME, "element", std::vector<uint64_t>{100,101,102});
	gltfBuilder.GetModel().meshes.emplace_back();
	int v = 0;
	AddMeshPrimitive({.gltfBuilder = gltfBuilder,
		.patches = {{{v++,0},{v++,0},{v++,0}}, {{v++,1},{v++,1},{v++,1}}},
		.material = 0});
	AddMeshPrimitive({.gltfBuilder = gltfBuilder,
		.patches = {{{v++,0},{v++,0},{v++,0}}, {{v++,2},{v++,2},{v++,2}}},
		.material = 1});
	CheckGltf(gltfBuilder.GetModel(), BeUtils::GltfTuner().Tune(gltfBuilder.GetModel()));
}

TEST_CASE("TestNoMergeIncompatibleMode")
{
	BeUtils::GltfBuilder gltfBuilder;
	gltfBuilder.AddMetadataProperty(FEATURE_TABLE_NAME, "element", std::vector<uint64_t>{100,101,102});
	gltfBuilder.GetModel().meshes.emplace_back();
	int v = 0;
	AddMeshPrimitive({.gltfBuilder = gltfBuilder,
		.patches = {{{v++,0},{v++,0}}, {{v++,1},{v++,1}}},
		.mode = CesiumGltf::MeshPrimitive::Mode::LINES});
	AddMeshPrimitive({.gltfBuilder = gltfBuilder,
		.patches = {{{v++,0},{v++,0},{v++,0}}, {{v++,2},{v++,2},{v++,2}}}});
	CheckGltf(gltfBuilder.GetModel(), BeUtils::GltfTuner().Tune(gltfBuilder.GetModel()));
}

TEST_CASE("TestNoMergeDifferentHasNormal")
{
	BeUtils::GltfBuilder gltfBuilder;
	gltfBuilder.AddMetadataProperty(FEATURE_TABLE_NAME, "element", std::vector<uint64_t>{100,101,102});
	gltfBuilder.GetModel().meshes.emplace_back();
	int v = 0;
	AddMeshPrimitive({.gltfBuilder = gltfBuilder,
		.patches = {{{v++,0},{v++,0},{v++,0}}, {{v++,2},{v++,2},{v++,2}}},
		.normalFormat = {g_ComponentTypeNoData}});
	AddMeshPrimitive({.gltfBuilder = gltfBuilder,
		.patches = {{{v++,0},{v++,0},{v++,0}}, {{v++,1},{v++,1},{v++,1}}}});
	CheckGltf(gltfBuilder.GetModel(), BeUtils::GltfTuner().Tune(gltfBuilder.GetModel()));
}

TEST_CASE("TestNoMergeDifferentHasUV")
{
	BeUtils::GltfBuilder gltfBuilder;
	gltfBuilder.AddMetadataProperty(FEATURE_TABLE_NAME, "element", std::vector<uint64_t>{100,101,102});
	gltfBuilder.GetModel().meshes.emplace_back();
	int v = 0;
	AddMeshPrimitive({.gltfBuilder = gltfBuilder,
		.patches = {{{v++,0},{v++,0},{v++,0}}, {{v++,2},{v++,2},{v++,2}}},
		.uvFormat = {g_ComponentTypeNoData}});
	AddMeshPrimitive({.gltfBuilder = gltfBuilder,
		.patches = {{{v++,0},{v++,0},{v++,0}}, {{v++,1},{v++,1},{v++,1}}}});
	CheckGltf(gltfBuilder.GetModel(), BeUtils::GltfTuner().Tune(gltfBuilder.GetModel()));
}

TEST_CASE("TestNoMergeDifferentHasColor")
{
	BeUtils::GltfBuilder gltfBuilder;
	gltfBuilder.AddMetadataProperty(FEATURE_TABLE_NAME, "element", std::vector<uint64_t>{100,101,102});
	gltfBuilder.GetModel().meshes.emplace_back();
	int v = 0;
	AddMeshPrimitive({.gltfBuilder = gltfBuilder,
		.patches = {{{v++,0},{v++,0},{v++,0}}, {{v++,2},{v++,2},{v++,2}}},
		.colorFormat = {g_ComponentTypeNoData}});
	AddMeshPrimitive({.gltfBuilder = gltfBuilder,
		.patches = {{{v++,0},{v++,0},{v++,0}}, {{v++,1},{v++,1},{v++,1}}}});
	CheckGltf(gltfBuilder.GetModel(), BeUtils::GltfTuner().Tune(gltfBuilder.GetModel()));
}

TEST_CASE("TestNoMergeDifferentHasFeatureId")
{
	BeUtils::GltfBuilder gltfBuilder;
	gltfBuilder.AddMetadataProperty(FEATURE_TABLE_NAME, "element", std::vector<uint64_t>{100,101,102});
	gltfBuilder.GetModel().meshes.emplace_back();
	int v = 0;
	AddMeshPrimitive({.gltfBuilder = gltfBuilder,
		.patches = {{{v++,0},{v++,0},{v++,0}}, {{v++,2},{v++,2},{v++,2}}},
		.featureIdFormat = {g_ComponentTypeNoData}});
	AddMeshPrimitive({.gltfBuilder = gltfBuilder,
		.patches = {{{v++,0},{v++,0},{v++,0}}, {{v++,1},{v++,1},{v++,1}}}});
	CheckGltf(gltfBuilder.GetModel(), BeUtils::GltfTuner().Tune(gltfBuilder.GetModel()));
}

TEST_CASE("TestSplitAndMerge")
{
	BeUtils::GltfBuilder gltfBuilder;
	gltfBuilder.AddMetadataProperty(FEATURE_TABLE_NAME, "element", std::vector<uint64_t>{100,101,102});
	gltfBuilder.GetModel().meshes.emplace_back();
	int v = 0;
	AddMeshPrimitive({.gltfBuilder = gltfBuilder,
		.patches = {{{v++,0},{v++,0},{v++,0}}, {{v++,1},{v++,1},{v++,1}}},
		.material = 0});
	AddMeshPrimitive({.gltfBuilder = gltfBuilder,
		.patches = {{{v++,0},{v++,0},{v++,0}}, {{v++,2},{v++,2},{v++,2}}},
		.material = 1});
	BeUtils::GltfTuner tuner;
	tuner.SetRules({{{{101,102}, 2}}});
	BeUtils::GltfBuilder expectedBuilder;
	expectedBuilder.AddMetadataProperty(FEATURE_TABLE_NAME, "element", std::vector<uint64_t>{100,101,102});
	expectedBuilder.GetModel().meshes.emplace_back();
	AddMeshPrimitive({.gltfBuilder = expectedBuilder,
		.patches = {{{0,0},{1,0},{2,0}}},
		.material = 0});
	AddMeshPrimitive({.gltfBuilder = expectedBuilder,
		.patches = {{{6,0},{7,0},{8,0}}},
		.material = 1});
	AddMeshPrimitive({.gltfBuilder = expectedBuilder,
		.patches = {{{3,1},{4,1},{5,1}}, {{9,2},{10,2},{11,2}}},
		.material = 2});
	CheckGltf(expectedBuilder.GetModel(), tuner.Tune(gltfBuilder.GetModel()));
}

TEST_CASE("TestPropertyTableValuesIndex")
{
	// This test verifies that PropertyTableProperty::values is correctly set by the tuner.
	// To do so, we add the property table after adding the primitives.
	// The tuner adds the property table before the primitives, so it should adjust this index.
	BeUtils::GltfBuilder gltfBuilder;
	gltfBuilder.GetModel().meshes.emplace_back();
	int v = 0;
	AddMeshPrimitive({.gltfBuilder = gltfBuilder,
		.patches = {{{v++,0},{v++,0},{v++,0}}}});
	gltfBuilder.AddMetadataProperty(FEATURE_TABLE_NAME, "element", std::vector<uint64_t>{100,101,102});
	BeUtils::GltfBuilder expectedBuilder;
	expectedBuilder.AddMetadataProperty(FEATURE_TABLE_NAME, "element", std::vector<uint64_t>{100,101,102});
	expectedBuilder.GetModel().meshes.emplace_back();
	AddMeshPrimitive({.gltfBuilder = expectedBuilder,
		.patches = {{{0,0},{1,0},{2,0}}}});
	CheckGltf(expectedBuilder.GetModel(), BeUtils::GltfTuner().Tune(gltfBuilder.GetModel()));
}

TEST_CASE("TestMergeWithMaterialFeatureId")
{
	BeUtils::GltfBuilder gltfBuilder;
	gltfBuilder.AddMetadataProperty(FEATURE_TABLE_NAME, "element", std::vector<uint64_t>{100, 101, 102, 103});
	gltfBuilder.AddMetadataProperty(FEATURE_TABLE_NAME, "model", std::vector<uint64_t>{200, 201, 202, 203});
	gltfBuilder.AddMetadataProperty(FEATURE_TABLE_NAME, "subcategory", std::vector<uint64_t>{300, 301, 302, 303});
	gltfBuilder.AddMetadataProperty(MATID_TABLE_NAME, "material", std::vector<uint64_t>{0x1981, 0x1982, 0x1983, 0x1984}, 1);
	gltfBuilder.GetModel().meshes.emplace_back();
	int v = 0;
	AddMeshPrimitive({ .gltfBuilder = gltfBuilder,
		.patches = {{{v++,0},{v++,0},{v++,0}}, {{v++,3},{v++,3},{v++,3}}},
		.materialFeatureIdFormat = {g_ComponentTypeAuto} });
	AddMeshPrimitive({ .gltfBuilder = gltfBuilder,
		.patches = {{{v++,0},{v++,0},{v++,0}}, {{v++,2},{v++,2},{v++,2}}},
		.materialFeatureIdFormat = {g_ComponentTypeAuto} });
	BeUtils::GltfBuilder expectedBuilder;
	expectedBuilder.AddMetadataProperty(FEATURE_TABLE_NAME, "element", std::vector<uint64_t>{100, 101, 102, 103});
	expectedBuilder.AddMetadataProperty(FEATURE_TABLE_NAME, "model", std::vector<uint64_t>{200, 201, 202, 203});
	expectedBuilder.AddMetadataProperty(FEATURE_TABLE_NAME, "subcategory", std::vector<uint64_t>{300, 301, 302, 303});
	expectedBuilder.AddMetadataProperty(MATID_TABLE_NAME, "material", std::vector<uint64_t>{0x1981, 0x1982, 0x1983, 0x1984}, 1);
	expectedBuilder.GetModel().meshes.emplace_back();
	// rules do not say to split materials, so the merge should occur
	AddMeshPrimitive({ .gltfBuilder = expectedBuilder,
		.patches = {{{0,0},{1,0},{2,0}}, {{3,3},{4,3},{5,3}}, {{6,0},{7,0},{8,0}}, {{9,2},{10,2},{11,2}}},
		.materialFeatureIdFormat = {g_ComponentTypeAuto} });
	CheckGltf(expectedBuilder.GetModel(), BeUtils::GltfTuner().Tune(gltfBuilder.GetModel()));
}

TEST_CASE("TestNoMergeDifferentHasMaterialFeatureId")
{
	BeUtils::GltfBuilder gltfBuilder;
	gltfBuilder.AddMetadataProperty(FEATURE_TABLE_NAME, "element", std::vector<uint64_t>{100, 101, 102, 103});
	gltfBuilder.AddMetadataProperty(FEATURE_TABLE_NAME, "model", std::vector<uint64_t>{200, 201, 202, 203});
	gltfBuilder.AddMetadataProperty(FEATURE_TABLE_NAME, "subcategory", std::vector<uint64_t>{300, 301, 302, 303});
	gltfBuilder.AddMetadataProperty(MATID_TABLE_NAME, "material", std::vector<uint64_t>{0x1981, 0x1982, 0x1983, 0x1984}, 1);
	gltfBuilder.GetModel().meshes.emplace_back();
	int v = 0;
	AddMeshPrimitive({ .gltfBuilder = gltfBuilder,
		.patches = {{{v++,0},{v++,0},{v++,0}}, {{v++,3},{v++,3},{v++,3}}} });
	AddMeshPrimitive({ .gltfBuilder = gltfBuilder,
		.patches = {{{v++,0},{v++,0},{v++,0}}, {{v++,2},{v++,2},{v++,2}}},
		.materialFeatureIdFormat = {g_ComponentTypeAuto} });
	CheckGltf(gltfBuilder.GetModel(), BeUtils::GltfTuner().Tune(gltfBuilder.GetModel()));
}

TEST_CASE("TestSplitMaterialFeatureId")
{
	BeUtils::GltfBuilder gltfBuilder;
	gltfBuilder.AddMetadataProperty(FEATURE_TABLE_NAME, "element", std::vector<uint64_t>{100, 101, 102, 103});
	gltfBuilder.AddMetadataProperty(MATID_TABLE_NAME, "material", std::vector<uint64_t>{0x1981, 0x1982, 0x1983, 0x1984}, 1);
	gltfBuilder.GetModel().meshes.emplace_back();
	int v = 0;
	AddMeshPrimitive({ .gltfBuilder = gltfBuilder,
		.patches = {{{v++,0},{v++,0},{v++,0}}, {{v++,3},{v++,3},{v++,3}}, {{v++,2},{v++,2},{v++,2}}},
		.materialFeatureIdFormat = {g_ComponentTypeAuto} });
	AddMeshPrimitive({ .gltfBuilder = gltfBuilder,
		.patches = {{{v++,0},{v++,0},{v++,0}}, {{v++,2},{v++,2},{v++,2}}, {{v++,3},{v++,3},{v++,3}}},
		.materialFeatureIdFormat = {g_ComponentTypeAuto} });
	BeUtils::GltfBuilder expectedBuilder;
	expectedBuilder.AddMetadataProperty(FEATURE_TABLE_NAME, "element", std::vector<uint64_t>{100, 101, 102, 103});
	expectedBuilder.AddMetadataProperty(MATID_TABLE_NAME, "material", std::vector<uint64_t>{0x1981, 0x1982, 0x1983, 0x1984}, 1);
	expectedBuilder.GetModel().meshes.emplace_back();
	AddMeshPrimitive({ .gltfBuilder = expectedBuilder,
		.patches = {{{0,0},{1,0},{2,0}}, {{9,0},{10,0},{11,0}}},
		.materialFeatureIdFormat = {g_ComponentTypeAuto},
		.iTwinMaterialId = {0x1981} });
	AddMeshPrimitive({ .gltfBuilder = expectedBuilder,
		.patches = {{{6,2},{7,2},{8,2}}, {{12,2},{13,2},{14,2}}},
		.materialFeatureIdFormat = {g_ComponentTypeAuto},
		.iTwinMaterialId = {0x1983} });
	AddMeshPrimitive({ .gltfBuilder = expectedBuilder,
		.patches = {{{3,3},{4,3},{5,3}}, {{15,3},{16,3},{17,3}}},
		.materialFeatureIdFormat = {g_ComponentTypeAuto},
		.iTwinMaterialId = {0x1984} });
	BeUtils::GltfTuner tuner;
	tuner.SetRules({ .itwinMatIDsToSplit_ = {{0x1981, 0x1982, 0x1983, 0x1984}} });
	CheckGltf(expectedBuilder.GetModel(), tuner.Tune(gltfBuilder.GetModel()));
}

TEST_CASE("TestSplitMaterialFeatureId_OneMaterial")
{
	BeUtils::GltfBuilder gltfBuilder;
	gltfBuilder.AddMetadataProperty(FEATURE_TABLE_NAME, "element", std::vector<uint64_t>{100, 101, 102, 103});
	gltfBuilder.AddMetadataProperty(MATID_TABLE_NAME, "material", std::vector<uint64_t>{0x1981, 0x1982, 0x1983, 0x1984}, 1);
	gltfBuilder.GetModel().meshes.emplace_back();
	int v = 0;
	AddMeshPrimitive({ .gltfBuilder = gltfBuilder,
		.patches = {{{v++,0},{v++,0},{v++,0}}, {{v++,3},{v++,3},{v++,3}}, {{v++,2},{v++,2},{v++,2}}},
		.materialFeatureIdFormat = {g_ComponentTypeAuto} });
	AddMeshPrimitive({ .gltfBuilder = gltfBuilder,
		.patches = {{{v++,0},{v++,0},{v++,0}}, {{v++,2},{v++,2},{v++,2}}, {{v++,3},{v++,3},{v++,3}}},
		.materialFeatureIdFormat = {g_ComponentTypeAuto} });
	BeUtils::GltfBuilder expectedBuilder;
	expectedBuilder.AddMetadataProperty(FEATURE_TABLE_NAME, "element", std::vector<uint64_t>{100, 101, 102, 103});
	expectedBuilder.AddMetadataProperty(MATID_TABLE_NAME, "material", std::vector<uint64_t>{0x1981, 0x1982, 0x1983, 0x1984}, 1);
	expectedBuilder.GetModel().meshes.emplace_back();
	AddMeshPrimitive({ .gltfBuilder = expectedBuilder,
		.patches = {{{0,0},{1,0},{2,0}}, {{6,2},{7,2},{8,2}}, {{9,0},{10,0},{11,0}}, {{12,2},{13,2},{14,2}}},
		.materialFeatureIdFormat = {g_ComponentTypeAuto} });
	AddMeshPrimitive({ .gltfBuilder = expectedBuilder,
		.patches = {{{3,3},{4,3},{5,3}}, {{15,3},{16,3},{17,3}}},
		.materialFeatureIdFormat = {g_ComponentTypeAuto},
		.iTwinMaterialId = {0x1984} });
	BeUtils::GltfTuner tuner;
	tuner.SetRules({ .itwinMatIDsToSplit_ = {{0x1984}} }); // correspond to material ID #3
	CheckGltf(expectedBuilder.GetModel(), tuner.Tune(gltfBuilder.GetModel()));
}