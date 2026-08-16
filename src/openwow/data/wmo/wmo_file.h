#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace openwow::data::wmo {

struct ChunkHeader {
  uint32_t magic;
  uint32_t size;
};
static_assert(sizeof(ChunkHeader) == 8, "ChunkHeader must be 8 bytes.");

struct WmoHeader {
  uint32_t nMaterials;
  uint32_t nGroups;
  uint32_t nPortals;
  uint32_t nLights;
  uint32_t nDoodadNames;
  uint32_t nDoodadDefs;
  uint32_t nDoodadSets;
  uint32_t ambientColor;
  uint32_t wmoID;
  float    boundingBox1[3];
  float    boundingBox2[3];
  uint32_t flags;
};
static_assert(sizeof(WmoHeader) == 64, "WmoHeader must be 64 bytes.");

enum WmoRootFlags : uint32_t {
  kWmoFlagNoPortalAtten  = 0x01,
  kWmoFlagUnifiedRender  = 0x02,
  kWmoFlagUseDbcLiquid   = 0x04,
  kWmoFlagHasVertexColor = 0x08,
};

struct WmoMaterial {
  uint32_t flags;
  uint32_t shader;
  uint32_t blendMode;
  uint32_t texture1Ofs;
  uint32_t sidnColor;
  uint32_t frameSidnColor;
  uint32_t texture2Ofs;
  uint32_t diffuseColor;
  uint32_t groundType;
  uint32_t texture3Ofs;
  uint32_t color2;
  uint32_t flags2;
  uint32_t runTimeData[4];
};
static_assert(sizeof(WmoMaterial) == 64, "WmoMaterial must be 64 bytes.");

enum WmoMaterialFlags : uint32_t {
  kMatUnlit       = 0x01,
  kMatUnfogged    = 0x02,
  kMatTwoSided    = 0x04,
  kMatExteriorLit = 0x08,
  kMatSidnNight   = 0x10,
  kMatWindow      = 0x20,
  kMatClampS      = 0x40,
  kMatClampT      = 0x80,
};

enum WmoShaderType : uint32_t {
  kShaderDiffuse   = 0,
  kShaderSpecular  = 1,
  kShaderMetal     = 2,
  kShaderEnv       = 3,
  kShaderOpaque    = 4,
  kShaderEnvMetal  = 5,
  kShaderTwoLayer  = 6,
};

struct WmoGroupInfo {
  uint32_t flags;
  float    boundingBox1[3];
  float    boundingBox2[3];
  int32_t  nameOffset;
};
static_assert(sizeof(WmoGroupInfo) == 32, "WmoGroupInfo must be 32 bytes.");

struct WmoPortal {
  uint16_t startVertex;
  uint16_t nVertices;
  float    normal[3];
  float    distance;
};
static_assert(sizeof(WmoPortal) == 20, "WmoPortal must be 20 bytes.");

struct WmoPortalRef {
  uint16_t portalIndex;
  uint16_t groupIndex;
  int16_t  side;
  uint16_t padding;
};
static_assert(sizeof(WmoPortalRef) == 8, "WmoPortalRef must be 8 bytes.");

struct WmoLight {
  uint8_t  type;
  uint8_t  useAttenuation;
  uint16_t padding;
  uint32_t color;
  float    position[3];
  float    intensity;
  float    attenuationStart;
  float    attenuationEnd;
  float    unknown[4];
};
static_assert(sizeof(WmoLight) == 48, "WmoLight must be 48 bytes.");

struct WmoDoodadSet {
  char     name[20];
  uint32_t startDoodad;
  uint32_t nDoodads;
  uint32_t padding;
};
static_assert(sizeof(WmoDoodadSet) == 32, "WmoDoodadSet must be 32 bytes.");

struct WmoDoodadDef {
  uint32_t nameOffset;
  float    position[3];
  float    orientation[4];
  float    scale;
  uint32_t color;
};
static_assert(sizeof(WmoDoodadDef) == 40, "WmoDoodadDef must be 40 bytes.");

struct WmoFog {
  uint32_t flags;
  float    position[3];
  float    smallerRadius;
  float    largerRadius;
  float    fogEnd;
  float    fogStartScalar;
  uint32_t fogColor;
  float    uwFogEnd;
  float    uwFogStartScalar;
  uint32_t uwFogColor;
};
static_assert(sizeof(WmoFog) == 48, "WmoFog must be 48 bytes.");

struct WmoConvexVolumePlane {
  float normal[3];
  float distance;
};
static_assert(sizeof(WmoConvexVolumePlane) == 16, "WmoConvexVolumePlane must be 16 bytes.");

enum WmoGroupFlags : uint32_t {
  kMogpHasBsp          = 0x00000001,
  kMogpHasLightmap     = 0x00000002,
  kMogpHasVertexColors = 0x00000004,
  kMogpExterior        = 0x00000008,
  kMogpExteriorLit     = 0x00000040,
  kMogpUnreachable     = 0x00000080,

  kMogpShowExteriorSky = 0x00000100,
  kMogpHasLights       = 0x00000200,
  kMogpHasLod          = 0x00000400,
  kMogpHasDoodads      = 0x00000800,
  kMogpHasWater        = 0x00001000,
  kMogpIndoor          = 0x00002000,
  kMogpAlwaysDraw      = 0x00010000,

  kMogpHasMoriMorb     = 0x00020000,
  kMogpShowSkybox      = 0x00040000,
  kMogpIsOcean         = 0x00080000,
  kMogpHasSecondMocv   = 0x01000000,
  kMogpHasSecondMotv   = 0x02000000,
};

struct WmoGroupHeader {
  uint32_t groupNameOfs;
  uint32_t descriptiveNameOfs;
  uint32_t flags;
  float    boundingBox1[3];
  float    boundingBox2[3];
  uint16_t portalStart;
  uint16_t portalCount;
  uint16_t transBatchCount;
  uint16_t intBatchCount;
  uint16_t extBatchCount;
  uint16_t paddingOrBatch;
  uint8_t  fogIndices[4];
  uint32_t liquidType;
  uint32_t wmoGroupID;
  uint32_t unknown1;
  uint32_t unknown2;
};
static_assert(sizeof(WmoGroupHeader) == 68, "WmoGroupHeader must be 68 bytes.");

enum WmoTriangleFlag : uint8_t {
  kMopyFUnk01       = 0x01,
  kMopyNoCamCollide = 0x02,
  kMopyFDetail      = 0x04,
  kMopyFCollision   = 0x08,
  kMopyFHint        = 0x10,
  kMopyFRender      = 0x20,
  kMopyFCullObjects = 0x40,
  kMopyFCollideHit  = 0x80,
};

struct WmoTriangleMaterial {
  uint8_t flags;
  uint8_t materialId;
};
static_assert(sizeof(WmoTriangleMaterial) == 2, "WmoTriangleMaterial must be 2 bytes.");

struct WmoRenderBatch {
  int16_t  bboxMin[3];
  int16_t  bboxMax[3];
  uint32_t startIndex;
  uint16_t nIndices;
  uint16_t startVertex;
  uint16_t lastVertex;
  uint8_t  flags;
  uint8_t  materialId;
};
static_assert(sizeof(WmoRenderBatch) == 24, "WmoRenderBatch must be 24 bytes.");

struct WmoAlternateRenderBatch {
  uint32_t startIndex;
  uint16_t nIndices;
  uint16_t padding;
};
static_assert(sizeof(WmoAlternateRenderBatch) == 8,
              "MORB records must retain their 8-byte layout.");

struct WmoBspNode {
  uint16_t flags;
  int16_t  negChild;
  int16_t  posChild;
  uint16_t nFaces;
  uint32_t faceStart;
  float    planeDist;
};
static_assert(sizeof(WmoBspNode) == 16, "WmoBspNode must be 16 bytes.");

struct WmoLiquidHeader {
  uint32_t xVerts;
  uint32_t yVerts;
  uint32_t xTiles;
  uint32_t yTiles;
  float    baseCoord[3];
  uint16_t materialId;
};

struct WmoVertexColor {
  uint8_t b, g, r, a;
};
static_assert(sizeof(WmoVertexColor) == 4, "WmoVertexColor must be 4 bytes.");

struct Vec3f {
  float x, y, z;
};
static_assert(sizeof(Vec3f) == 12, "Vec3f must be 12 bytes.");

struct Vec2f {
  float u, v;
};
static_assert(sizeof(Vec2f) == 8, "Vec2f must be 8 bytes.");

struct WmoVisibleBlock {
  uint32_t value{};
};
static_assert(sizeof(WmoVisibleBlock) == 4,
              "MOVB records must retain their retail 4-byte layout.");

struct WmoLiquidData {
  WmoLiquidHeader header{};
  std::vector<uint8_t> vertexData;
  std::vector<uint8_t> tileFlags;
};

struct WmoGroup {
  WmoGroupHeader header{};

  std::vector<WmoTriangleMaterial> triangleMaterials;
  std::vector<uint16_t>            indices;
  std::vector<Vec3f>               vertices;
  std::vector<Vec3f>               normals;
  std::vector<Vec2f>               texCoords;
  std::vector<WmoRenderBatch>      renderBatches;

  std::vector<uint16_t>            lightRefs;
  std::vector<uint16_t>            doodadRefs;
  std::vector<WmoBspNode>          bspNodes;
  std::vector<uint16_t>            bspFaceIndices;
  std::vector<WmoVertexColor>      vertexColors;
  std::vector<uint16_t>            alternateIndices;
  std::vector<WmoAlternateRenderBatch> alternateRenderBatches;
  std::vector<Vec2f>               texCoords2;
  std::vector<WmoVertexColor>      vertexColors2;

  bool hasLiquid{false};
  WmoLiquidData liquid;
};

struct WmoRoot {
  uint32_t version{0};
  WmoHeader header{};

  std::vector<uint8_t> textureNames;
  std::vector<uint8_t> groupNames;
  std::string          skyboxName;
  std::vector<uint8_t> doodadNames;

  std::vector<WmoMaterial>          materials;
  std::vector<WmoGroupInfo>         groupInfos;
  std::vector<Vec3f>                portalVertices;
  std::vector<WmoPortal>            portals;
  std::vector<WmoPortalRef>         portalRefs;
  std::vector<Vec3f>                visibleVertices;

  std::vector<WmoVisibleBlock>      visibleBlocks;
  std::vector<WmoLight>             lights;
  std::vector<WmoDoodadSet>         doodadSets;
  std::vector<WmoDoodadDef>         doodadDefs;
  std::vector<WmoFog>               fogs;
  std::vector<WmoConvexVolumePlane> convexVolumePlanes;
};

struct WmoFile {
  WmoRoot               root;
  std::vector<WmoGroup> groups;
};

struct WmoLoadResult {
  bool        ok{false};
  std::string error;
  WmoRoot     root;
};

struct WmoGroupLoadResult {
  bool        ok{false};
  std::string error;
  WmoGroup    group;
};

WmoLoadResult LoadWmoRoot(const uint8_t* data, size_t size);

WmoLoadResult LoadWmoRoot(const std::vector<uint8_t>& data);

WmoGroupLoadResult LoadWmoGroup(const uint8_t* data, size_t size);

WmoGroupLoadResult LoadWmoGroup(const std::vector<uint8_t>& data);

std::string LookupStringInBlock(const std::vector<uint8_t>& block, uint32_t offset);

}
