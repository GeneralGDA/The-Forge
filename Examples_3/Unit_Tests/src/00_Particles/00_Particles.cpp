#include "00_Emitter.h"

#include "../../../../Common_3/OS/Interfaces/ICameraController.h"
#include "../../../../Common_3/OS/Interfaces/IApp.h"
#include "../../../../Common_3/OS/Interfaces/ILogManager.h"
#include "../../../../Common_3/OS/Interfaces/IFileSystem.h"
#include "../../../../Common_3/OS/Interfaces/ITimeManager.h"
#include "../../../../Common_3/OS/Core/DebugRenderer.h"
#include "../../../../Common_3/Renderer/IRenderer.h"
#include "../../../../Common_3/Renderer/ResourceLoader.h"
#include "../../../../Common_3/OS/Math/MathTypes.h"
#include "../../../../Common_3/OS/Interfaces/IMemoryManager.h"

#include "../../../../Middleware_3/UI/AppUI.h"
#include "../../../../Middleware_3/Input/InputSystem.h"
#include "../../../../Middleware_3/Input/InputMappings.h"

#include "../../../../Common_3/ThirdParty/OpenSource/TinySTL/vector.h"
#include "../../../../Common_3/ThirdParty/OpenSource/TinySTL/string.h"

#if defined(TARGET_IOS) || defined(__ANDROID__)
#define NEED_JOYSTICK
#endif

struct UniformBlock final
{
	mat4 mProjectView;
	mat4 mCamera;
	vec4 zProjection;

	// Point Light Information
	vec3 mLightPosition;
	vec3 mLightColor;
};

struct ShadowReceiverUniform final
{
	mat4 shadowMapMvp;
	mat4 shadowMapCamera;
};

namespace
{

const int PARTICLES_STYLES_COUNT = 3;
const int MAX_PARTICLES_COUNT = 200;

const int CONST_BUFFER_QUANT_SIZE = 4;

} // namespace

struct ParticlesUniform final
{
	float positions[CONST_BUFFER_QUANT_SIZE * MAX_PARTICLES_COUNT];
	float timeAndStyle[CONST_BUFFER_QUANT_SIZE * MAX_PARTICLES_COUNT];

	vec4 colorAndSizeScale[PARTICLES_STYLES_COUNT];
	
	float particlesLifeLength;
};

const auto PRE_RENDERED_FRAMES_COUNT = 3u;

Renderer* renderer = nullptr;

Queue* commandQueue = nullptr;
CmdPool* commandsPool = nullptr;
Cmd** commandsBuffers = nullptr;

SwapChain* swapChain = nullptr;
RenderTarget* depthBuffer = nullptr;
Fence* frameCompleteFences[PRE_RENDERED_FRAMES_COUNT] = { nullptr };
Semaphore* imageAcquiredSemaphore = nullptr;
Semaphore* frameCompleteSemaphores[PRE_RENDERED_FRAMES_COUNT] = { nullptr };

Shader* skyBoxShader = nullptr;
Buffer* skyBoxVertexBuffer = nullptr;
Pipeline* skyBoxPipeline = nullptr;
RootSignature* skyBoxRootSignature = nullptr;
Sampler* skyBoxSampler = nullptr;

const auto SKY_BOX_FACES_COUNT = 6;
Texture* skyBoxTextures[SKY_BOX_FACES_COUNT];

#if defined(NEED_JOYSTICK)
VirtualJoystickUI gVirtualJoystick;
#endif

DepthState* depthTestEnabledState = nullptr;
RasterizerState* skyBoxRasterizerState = nullptr;

Buffer* commonUniformBuffers[PRE_RENDERED_FRAMES_COUNT] = { nullptr };
Buffer* skyboxUniformBuffers[PRE_RENDERED_FRAMES_COUNT] = { nullptr };

uint32_t preRenderedFrameIndex = 0;

UniformBlock commonUniformData;
UniformBlock skyBoxUniformData;

ICameraController* cameraController = nullptr;

UIApp userInterface;

FileSystem fileSystem;
LogManager logManager;

const char* skyBoxFaceImageFileNames[SKY_BOX_FACES_COUNT] =
{
	"Skybox2_right1.png",
	"Skybox2_left2.png",
	"Skybox2_top3.png",
	"Skybox2_bottom4.png",
	"Skybox2_front5.png",
	"Skybox2_back6.png",
};

const char* pszBases[] =
{
	"../../../src/00_Particles/",		// FSR_BinShaders
	"../../../src/00_Particles/",		// FSR_SrcShaders
	"../../../UnitTestResources/",		// FSR_BinShaders_Common
	"../../../UnitTestResources/",		// FSR_SrcShaders_Common
	"../../../UnitTestResources/",		// FSR_Textures
	"../../../UnitTestResources/",		// FSR_Meshes
	"../../../UnitTestResources/",		// FSR_Builtin_Fonts
	"../../../src/00_Particles/",		// FSR_GpuConfig
	"",									// FSR_OtherFiles
};

TextDrawDesc gFrameTimeDraw = TextDrawDesc(0, 0xff000000, 18);

class Particles final : public IApp
{
private:

	static const int FLOOR_VERTEX_COUNT = 6;
	static const int SHADOW_MAP_WIDTH = 1024;
	static const int SHADOW_MAP_HEIGHT = 1024;

	Shader* floorShader = nullptr;
	Buffer* floorVertices = nullptr;
	Pipeline* floorPipeline = nullptr;
	RasterizerState* floorRasterizerState = nullptr;
	Sampler* shadowMapSampler = nullptr;
	RootSignature* floorRootSignature = nullptr;
	Buffer* shadowReceiversUniformBuffer[PRE_RENDERED_FRAMES_COUNT] = { nullptr };

	static const int maxParticlesCount = 1;
	static const int particleVertexCount = 6;
	Shader* particlesShader = nullptr;
	Buffer* particleVertices = nullptr;
	Pipeline* particlesPipeline = nullptr;
	DepthState* particlesDepthState = nullptr;
	BlendState* particlesBlendState = nullptr;
	RasterizerState* particlesRasterizerState = nullptr;
	Texture* particlesTexture = nullptr;
	Sampler* particleImageSampler = nullptr;
	RootSignature* particlesRootSignature = nullptr;

	static const int SHADOW_MAP_BUFFERS_COUNT = 2;
	RenderTarget* shadowMapDepth = nullptr;
	RenderTarget* shadowMapColor = nullptr;
	Pipeline* shadowMapPassPipeline = nullptr;
	DepthState* depthStencilStateDisableAll = nullptr;
	Shader* particlesShadowShader = nullptr;

	mat4 lightView;
	mat4 lightProjection;
	UniformBlock shadowMapUniforms;
	ShadowReceiverUniform shadowReceiversUniforms;
	Buffer* shadowMapUniformsBuffers[PRE_RENDERED_FRAMES_COUNT] = { nullptr };
	Emitter emitter;

	ParticlesUniform particlesFinalRender;
	ParticlesUniform particlesShadowMap;

	Buffer* particlesPerInstanceData[PRE_RENDERED_FRAMES_COUNT] = { nullptr };
	Buffer* particlesPerInstanceDataShadowMap[PRE_RENDERED_FRAMES_COUNT] = { nullptr };

	void prepareFloorResources()
	{
		ShaderLoadDesc floorShaderSource = {};
		floorShaderSource.mStages[0] = { "floor.vert", nullptr, 0, FSR_SrcShaders };
		floorShaderSource.mStages[1] = { "floor.frag", nullptr, 0, FSR_SrcShaders };
		addShader(renderer, &floorShaderSource, &floorShader);

		const auto floorHalfSize = 100.0f;

		const auto FLOATS_PER_FLOOR_VERTEX = 3;

		const float floorMeshPositions[] =
		{
			-floorHalfSize, 0.0f, +floorHalfSize,
			+floorHalfSize, 0.0f, +floorHalfSize,
			+floorHalfSize, 0.0f, -floorHalfSize,

			-floorHalfSize, 0.0f, -floorHalfSize,
			-floorHalfSize, 0.0f, +floorHalfSize,
			+floorHalfSize, 0.0f, -floorHalfSize,
		};

		ASSERT(0 == (sizeof(floorMeshPositions) / sizeof(float)) % FLOATS_PER_FLOOR_VERTEX);
		ASSERT(FLOOR_VERTEX_COUNT * sizeof(float) * FLOATS_PER_FLOOR_VERTEX == sizeof(floorMeshPositions));

		BufferLoadDesc floorVertexBufferDescription = {};
		floorVertexBufferDescription.mDesc.mDescriptors = DESCRIPTOR_TYPE_VERTEX_BUFFER;
		floorVertexBufferDescription.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_GPU_ONLY;
		floorVertexBufferDescription.mDesc.mSize = sizeof(floorMeshPositions);
		floorVertexBufferDescription.mDesc.mVertexStride = sizeof(float) * FLOATS_PER_FLOOR_VERTEX;
		floorVertexBufferDescription.pData = floorMeshPositions;
		floorVertexBufferDescription.ppBuffer = &floorVertices;
		addResource(&floorVertexBufferDescription);

		RasterizerStateDesc floorRasterizerStateDescription = {};
		floorRasterizerStateDescription.mCullMode = CULL_MODE_FRONT;
		addRasterizerState(renderer, &floorRasterizerStateDescription, &floorRasterizerState);

		SamplerDesc shadowMapSamplerDescription = {};
		shadowMapSamplerDescription.mMinFilter = FILTER_LINEAR;
		shadowMapSamplerDescription.mMagFilter = FILTER_LINEAR;
		shadowMapSamplerDescription.mMipMapMode = MIPMAP_MODE_LINEAR;
		shadowMapSamplerDescription.mAddressU = ADDRESS_MODE_CLAMP_TO_EDGE;
		shadowMapSamplerDescription.mAddressV = ADDRESS_MODE_CLAMP_TO_EDGE;
		shadowMapSamplerDescription.mAddressW = ADDRESS_MODE_CLAMP_TO_EDGE;
		shadowMapSamplerDescription.mMipLosBias = 0;
		shadowMapSamplerDescription.mMaxAnisotropy = 16;
		shadowMapSamplerDescription.mCompareFunc = CMP_ALWAYS;
		addSampler(renderer, &shadowMapSamplerDescription, &shadowMapSampler);

		{
			Shader* shadowReceiversShaders[] = { floorShader };
			const char* shadowReceiversSamplersNames[] = { "shadowMapSampler" };
			Sampler* shadowReceiversSamplers[] = { shadowMapSampler };

			RootSignatureDesc rootDescription = {};
			rootDescription.mStaticSamplerCount = _countof(shadowReceiversSamplersNames);
			rootDescription.ppStaticSamplerNames = shadowReceiversSamplersNames;
			rootDescription.ppStaticSamplers = shadowReceiversSamplers;
			rootDescription.mShaderCount = _countof(shadowReceiversShaders);
			rootDescription.ppShaders = shadowReceiversShaders;
			addRootSignature(renderer, &rootDescription, &floorRootSignature);
		}

		BufferLoadDesc shadowReceiversUniformsDescription = {};
		shadowReceiversUniformsDescription.mDesc.mDescriptors = DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		shadowReceiversUniformsDescription.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
		shadowReceiversUniformsDescription.mDesc.mSize = sizeof(ShadowReceiverUniform);
		shadowReceiversUniformsDescription.mDesc.mFlags = BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
		shadowReceiversUniformsDescription.pData = nullptr;
		for (auto& buffer : shadowReceiversUniformBuffer)
		{
			shadowReceiversUniformsDescription.ppBuffer = &buffer;
			addResource(&shadowReceiversUniformsDescription);
		}
	}

	void prepareParticlesResources()
	{
		ShaderLoadDesc particlesShaderSource = {};
		particlesShaderSource.mStages[0] = { "particle.vert", nullptr, 0, FSR_SrcShaders };
		particlesShaderSource.mStages[1] = { "particle.frag", nullptr, 0, FSR_SrcShaders };
		addShader(renderer, &particlesShaderSource, &particlesShader);

		ShaderLoadDesc particlesShadowDepthColorShaderSource = {};
		particlesShadowDepthColorShaderSource.mStages[0] = { "particle_shadow_map.vert", nullptr, 0, FSR_SrcShaders };
		particlesShadowDepthColorShaderSource.mStages[1] = { "particle_shadow_map.frag", nullptr, 0, FSR_SrcShaders };
		addShader(renderer, &particlesShadowDepthColorShaderSource, &particlesShadowShader);

		const auto FLOATS_PER_PARTICLE_VERTEX = 2;
		const auto VERTICES_PER_PARTICLE = 6;

		const float PARTICLE_BASE_HALF_SIZE = 0.5f;

		const float positions[] =
		{
			-PARTICLE_BASE_HALF_SIZE, +PARTICLE_BASE_HALF_SIZE,
			+PARTICLE_BASE_HALF_SIZE, +PARTICLE_BASE_HALF_SIZE,
			+PARTICLE_BASE_HALF_SIZE, -PARTICLE_BASE_HALF_SIZE,

			-PARTICLE_BASE_HALF_SIZE, -PARTICLE_BASE_HALF_SIZE,
			-PARTICLE_BASE_HALF_SIZE, +PARTICLE_BASE_HALF_SIZE,
			+PARTICLE_BASE_HALF_SIZE, -PARTICLE_BASE_HALF_SIZE,
		};

		ASSERT(0 == (sizeof(positions) / sizeof(float)) % FLOATS_PER_PARTICLE_VERTEX);

		BufferLoadDesc particlesVertexBufferDescription = {};
		particlesVertexBufferDescription.mDesc.mDescriptors = DESCRIPTOR_TYPE_VERTEX_BUFFER;
		particlesVertexBufferDescription.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_GPU_ONLY;
		particlesVertexBufferDescription.mDesc.mSize = sizeof(positions);
		particlesVertexBufferDescription.mDesc.mVertexStride = sizeof(float) * FLOATS_PER_PARTICLE_VERTEX;
		particlesVertexBufferDescription.pData = positions;
		particlesVertexBufferDescription.ppBuffer = &particleVertices;
		addResource(&particlesVertexBufferDescription);

		DepthStateDesc depthStateDescription = {};
		depthStateDescription.mDepthTest = true;
		depthStateDescription.mDepthWrite = false;
		depthStateDescription.mDepthFunc = CMP_LEQUAL;
		addDepthState(renderer, &depthStateDescription, &particlesDepthState);

		BlendStateDesc blendStateDescription = {};
		blendStateDescription.mIndependentBlend = false;
		blendStateDescription.mAlphaToCoverage = false;
		blendStateDescription.mRenderTargetMask = BLEND_STATE_TARGET_0 | BLEND_STATE_TARGET_1;
		
		blendStateDescription.mMasks[0] = ALL;
		blendStateDescription.mBlendAlphaModes[0] = BM_ADD;
		blendStateDescription.mBlendModes[0] = BM_ADD;
		blendStateDescription.mDstAlphaFactors[0] = BC_ZERO;
		blendStateDescription.mDstFactors[0] = BC_ONE_MINUS_SRC_ALPHA;
		blendStateDescription.mSrcAlphaFactors[0] = BC_ONE;
		blendStateDescription.mSrcFactors[0] = BC_SRC_ALPHA;
		
		blendStateDescription.mMasks[1] = ALL;
		blendStateDescription.mBlendAlphaModes[1] = BM_ADD;
		blendStateDescription.mBlendModes[1] = BM_ADD;
		blendStateDescription.mDstAlphaFactors[1] = BC_ZERO;
		blendStateDescription.mDstFactors[1] = BC_ZERO;
		blendStateDescription.mSrcAlphaFactors[1] = BC_ONE;
		blendStateDescription.mSrcFactors[1] = BC_ONE;
		
		addBlendState(renderer, &blendStateDescription, &particlesBlendState);

		TextureLoadDesc textureDescription = {};
		textureDescription.mRoot = FSR_Textures;
		textureDescription.mUseMipmaps = true;
		textureDescription.pFilename = "blackSmoke00.png";
		textureDescription.ppTexture = &particlesTexture;
		addResource(&textureDescription, true);

		RasterizerStateDesc rasterizerStateDescription = {};
		rasterizerStateDescription.mCullMode = CULL_MODE_NONE;
		addRasterizerState(renderer, &rasterizerStateDescription, &particlesRasterizerState);

		SamplerDesc samplerDescription = {};
		samplerDescription.mMinFilter = FILTER_LINEAR;
		samplerDescription.mMagFilter = FILTER_LINEAR;
		samplerDescription.mMipMapMode = MIPMAP_MODE_LINEAR;
		samplerDescription.mAddressU = ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerDescription.mAddressV = ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerDescription.mAddressW = ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerDescription.mMipLosBias = 0;
		samplerDescription.mMaxAnisotropy = 16;
		samplerDescription.mCompareFunc = CMP_ALWAYS;
		addSampler(renderer, &samplerDescription, &particleImageSampler);

		{
			Shader* particlesShaders[] = { particlesShader, particlesShadowShader };
			const char* staticSamplersNames[] = { "particleImageSampler" };
			Sampler* staticSamplers[] = { particleImageSampler };

			RootSignatureDesc rootDescription = {};
			rootDescription.mStaticSamplerCount = _countof(staticSamplersNames);
			rootDescription.ppStaticSamplerNames = staticSamplersNames;
			rootDescription.ppStaticSamplers = staticSamplers;
			rootDescription.mShaderCount = _countof(particlesShaders);
			rootDescription.ppShaders = particlesShaders;
			addRootSignature(renderer, &rootDescription, &particlesRootSignature);
		}

		BufferLoadDesc perInstanceBufferDescription = {};
		perInstanceBufferDescription.mDesc.mDescriptors = DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		perInstanceBufferDescription.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
		perInstanceBufferDescription.mDesc.mSize = sizeof(ParticlesUniform);
		perInstanceBufferDescription.mDesc.mFlags = BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
		perInstanceBufferDescription.pData = nullptr;
		for (auto& instance : particlesPerInstanceData)
		{
			perInstanceBufferDescription.ppBuffer = &instance;
			addResource(&perInstanceBufferDescription);
		}
		for (auto& instance : particlesPerInstanceDataShadowMap)
		{
			perInstanceBufferDescription.ppBuffer = &instance;
			addResource(&perInstanceBufferDescription);
		}

		particlesFinalRender.particlesLifeLength = Emitter::LIFE_LENGTH_SECONDS;
		particlesShadowMap.particlesLifeLength = Emitter::LIFE_LENGTH_SECONDS;

		auto styleCounter = 0;

		particlesFinalRender.colorAndSizeScale[styleCounter] = vec4{1.0f, 0.0f, 0.0f, 0.5f};
		particlesShadowMap.colorAndSizeScale[styleCounter++] = vec4{1.0f, 0.0f, 0.0f, 0.5f};

		particlesFinalRender.colorAndSizeScale[styleCounter] = vec4{0.0f, 1.0f, 0.0f, 0.8f};
		particlesShadowMap.colorAndSizeScale[styleCounter++] = vec4{0.0f, 1.0f, 0.0f, 0.8f};

		particlesFinalRender.colorAndSizeScale[styleCounter] = vec4{0.0f, 0.0f, 1.0f, 1.0f};
		particlesShadowMap.colorAndSizeScale[styleCounter++] = vec4{0.0f, 0.0f, 1.0f, 1.0f};
		
		ASSERT(styleCounter == _countof(particlesShadowMap.colorAndSizeScale));
		ASSERT(styleCounter == _countof(particlesFinalRender.colorAndSizeScale));

		DepthStateDesc disableAllDepthStateDescription = {};
		depthStateDescription.mDepthTest = false;
		depthStateDescription.mDepthWrite = false;
		depthStateDescription.mDepthFunc = CMP_ALWAYS;
		addDepthState(renderer, &disableAllDepthStateDescription, &depthStencilStateDisableAll);

		BufferLoadDesc shadowMapUniformsDescription = {};
		shadowMapUniformsDescription.mDesc.mDescriptors = DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		shadowMapUniformsDescription.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
		shadowMapUniformsDescription.mDesc.mSize = sizeof(UniformBlock);
		shadowMapUniformsDescription.mDesc.mFlags = BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
		shadowMapUniformsDescription.pData = nullptr;
		for (auto& buffer : shadowMapUniformsBuffers)
		{
			shadowMapUniformsDescription.ppBuffer = &buffer;
			addResource(&shadowMapUniformsDescription);
		}
	}

public:
	
	Particles()
		:
		emitter(MAX_PARTICLES_COUNT, PARTICLES_STYLES_COUNT)
	{
		const auto xRadians = degToRad(-40.0f);
		const auto yRadians = degToRad(180.0f);
		const auto lightPosition = vec4{ 30.0f, -66.0f, -100.0f, 1.0f };

		lightView = mat4::rotationXY(xRadians, yRadians);
		const vec4 translation = lightView * lightPosition;
		lightView.setTranslation(translation.getXYZ());
		
		lightProjection = mat4::perspective(PI / 2.0f, SHADOW_MAP_HEIGHT / static_cast<float>(SHADOW_MAP_WIDTH), 0.1f, 100.0f);
	}

	bool Init() override
	{
		RendererDesc settings = {};
		initRenderer(GetName(), &settings, &renderer);
		
		if (!renderer)
		{
			return false;
		}
			
		QueueDesc queueDesc = {};
		queueDesc.mType = CMD_POOL_DIRECT;
		addQueue(renderer, &queueDesc, &commandQueue);
		addCmdPool(renderer, commandQueue, false, &commandsPool);
		addCmd_n(commandsPool, false, PRE_RENDERED_FRAMES_COUNT, &commandsBuffers);

		for (uint32_t i = 0; i < PRE_RENDERED_FRAMES_COUNT; ++i)
		{
			addFence(renderer, &frameCompleteFences[i]);
			addSemaphore(renderer, &frameCompleteSemaphores[i]);
		}
		addSemaphore(renderer, &imageAcquiredSemaphore);

		initResourceLoaderInterface(renderer, DEFAULT_MEMORY_BUDGET, true);
		initDebugRendererInterface(renderer, "TitilliumText/TitilliumText-Bold.otf", FSR_Builtin_Fonts);

		ASSERT(SKY_BOX_FACES_COUNT == _countof(skyBoxTextures));
		ASSERT(SKY_BOX_FACES_COUNT == _countof(skyBoxFaceImageFileNames));
		for (int i = 0; i < SKY_BOX_FACES_COUNT; ++i)
		{
			TextureLoadDesc textureDesc = {};
			textureDesc.mRoot = FSR_Textures;
			textureDesc.mUseMipmaps = true;
			textureDesc.pFilename = skyBoxFaceImageFileNames[i];
			textureDesc.ppTexture = &skyBoxTextures[i];
			addResource(&textureDesc, true);
		}

		#if defined(__ANDROID__) || defined(TARGET_IOS)
		if (!gVirtualJoystick.Init(pRenderer, "circlepad.png", FSR_Textures))
		{
			LOGERRORF("Could not initialize Virtual Joystick.");
			return false;
		}
		#endif

		{
			prepareFloorResources();
			prepareParticlesResources();
		}

		ShaderLoadDesc skyShader = {};
		skyShader.mStages[0] = { "skybox.vert", NULL, 0, FSR_SrcShaders };
		skyShader.mStages[1] = { "skybox.frag", NULL, 0, FSR_SrcShaders };
		
		addShader(renderer, &skyShader, &skyBoxShader);

		SamplerDesc samplerDesc = 
		{
			FILTER_LINEAR, FILTER_LINEAR, MIPMAP_MODE_NEAREST,
			ADDRESS_MODE_CLAMP_TO_EDGE, ADDRESS_MODE_CLAMP_TO_EDGE, ADDRESS_MODE_CLAMP_TO_EDGE
		};
		addSampler(renderer, &samplerDesc, &skyBoxSampler);

		Shader* shaders[] = { skyBoxShader };
		const char* pStaticSamplersNames[] = { "uSampler0" };
		Sampler* staticSamplers[] = { skyBoxSampler };
		RootSignatureDesc rootDesc = {};
		rootDesc.mStaticSamplerCount = _countof(pStaticSamplersNames);
		rootDesc.ppStaticSamplerNames = pStaticSamplersNames;
		rootDesc.ppStaticSamplers = staticSamplers;
		rootDesc.mShaderCount = _countof(shaders);
		rootDesc.ppShaders = shaders;
		addRootSignature(renderer, &rootDesc, &skyBoxRootSignature);

		RasterizerStateDesc skyBoxRasterizerStateDescription = {};
		skyBoxRasterizerStateDescription.mCullMode = CULL_MODE_NONE;
		addRasterizerState(renderer, &skyBoxRasterizerStateDescription, &skyBoxRasterizerState);

		DepthStateDesc depthTestEnabledStateDescription = {};
		depthTestEnabledStateDescription.mDepthTest = true;
		depthTestEnabledStateDescription.mDepthWrite = true;
		depthTestEnabledStateDescription.mDepthFunc = CMP_LEQUAL;
		addDepthState(renderer, &depthTestEnabledStateDescription, &depthTestEnabledState);

		float skyBoxPoints[] = 
		{
			+10.0f, -10.0f, -10.0f, 6.0f, // -z
			-10.0f, -10.0f, -10.0f, 6.0f,
			-10.0f, +10.0f, -10.0f, 6.0f,
			-10.0f, +10.0f, -10.0f, 6.0f,
			+10.0f, +10.0f, -10.0f, 6.0f,
			+10.0f, -10.0f, -10.0f, 6.0f,
								    
			-10.0f, -10.0f, +10.0f, 2.0f,  //-x
			-10.0f, -10.0f, -10.0f, 2.0f,
			-10.0f, +10.0f, -10.0f, 2.0f,
			-10.0f, +10.0f, -10.0f, 2.0f,
			-10.0f, +10.0f, +10.0f, 2.0f,
			-10.0f, -10.0f, +10.0f, 2.0f,
								    
			+10.0f, -10.0f, -10.0f, 1.0f, //+x
			+10.0f, -10.0f, +10.0f, 1.0f,
			+10.0f, +10.0f, +10.0f, 1.0f,
			+10.0f, +10.0f, +10.0f, 1.0f,
			+10.0f, +10.0f, -10.0f, 1.0f,
			+10.0f, -10.0f, -10.0f, 1.0f,

			-10.0f, -10.0f, +10.0f, 5.0f,  // +z
			-10.0f, +10.0f, +10.0f, 5.0f,
			+10.0f, +10.0f, +10.0f, 5.0f,
			+10.0f, +10.0f, +10.0f, 5.0f,
			+10.0f, -10.0f, +10.0f, 5.0f,
			-10.0f, -10.0f, +10.0f, 5.0f,

			-10.0f, +10.0f, -10.0f, 3.0f,  //+y
			+10.0f, +10.0f, -10.0f, 3.0f,
			+10.0f, +10.0f, +10.0f, 3.0f,
			+10.0f, +10.0f, +10.0f, 3.0f,
			-10.0f, +10.0f, +10.0f, 3.0f,
			-10.0f, +10.0f, -10.0f, 3.0f,

			+10.0f, -10.0f, +10.0f, 4.0f,  //-y
			+10.0f, -10.0f, -10.0f, 4.0f,
			-10.0f, -10.0f, -10.0f, 4.0f,
			-10.0f, -10.0f, -10.0f, 4.0f,
			-10.0f, -10.0f, +10.0f, 4.0f,
			+10.0f, -10.0f, +10.0f, 4.0f,
		};

		uint64_t skyBoxVertexDataSize = 4 * 6 * SKY_BOX_FACES_COUNT * sizeof(float);
		BufferLoadDesc skyboxVbDesc = {};
		skyboxVbDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_VERTEX_BUFFER;
		skyboxVbDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_GPU_ONLY;
		skyboxVbDesc.mDesc.mSize = skyBoxVertexDataSize;
		skyboxVbDesc.mDesc.mVertexStride = sizeof(float) * 4;
		skyboxVbDesc.pData = skyBoxPoints;
		skyboxVbDesc.ppBuffer = &skyBoxVertexBuffer;
		addResource(&skyboxVbDesc);

		BufferLoadDesc commonUniformBufferbDescription = {};
		commonUniformBufferbDescription.mDesc.mDescriptors = DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		commonUniformBufferbDescription.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
		commonUniformBufferbDescription.mDesc.mSize = sizeof(UniformBlock);
		commonUniformBufferbDescription.mDesc.mFlags = BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
		commonUniformBufferbDescription.pData = nullptr;
		ASSERT(PRE_RENDERED_FRAMES_COUNT == _countof(commonUniformBuffers));
		ASSERT(PRE_RENDERED_FRAMES_COUNT == _countof(skyboxUniformBuffers));
		for (uint32_t i = 0; i < PRE_RENDERED_FRAMES_COUNT; ++i)
		{
			commonUniformBufferbDescription.ppBuffer = &commonUniformBuffers[i];
			addResource(&commonUniformBufferbDescription);
			commonUniformBufferbDescription.ppBuffer = &skyboxUniformBuffers[i];
			addResource(&commonUniformBufferbDescription);
		}

		finishResourceLoading();

		if (!userInterface.Init(renderer))
		{
			return false;
		}

		userInterface.LoadFont("TitilliumText/TitilliumText-Bold.otf", FSR_Builtin_Fonts);

		const CameraMotionParameters cmp { 160.0f, 600.0f, 200.0f };
		const vec3 camPos { 48.0f, 48.0f, 20.0f };
		const vec3 lookAt { 0 };

		cameraController = createFpsCameraController(camPos, lookAt);
		requestMouseCapture(true);

		cameraController->setMotionParameters(cmp);

		InputSystem::RegisterInputEvent(cameraInputEvent);

		return true;
	}

	void Exit() override
	{
		waitForFences(commandQueue, 1, &frameCompleteFences[preRenderedFrameIndex], true);

		destroyCameraController(cameraController);

		removeDebugRendererInterface();

		#if defined(NEED_JOYSTICK)
		gVirtualJoystick.Exit();
		#endif

		userInterface.Exit();

		ASSERT(PRE_RENDERED_FRAMES_COUNT == _countof(commonUniformBuffers));
		ASSERT(PRE_RENDERED_FRAMES_COUNT == _countof(skyboxUniformBuffers));
		for (uint32_t i = 0; i < PRE_RENDERED_FRAMES_COUNT; ++i)
		{
			removeResource(commonUniformBuffers[i]);
			removeResource(skyboxUniformBuffers[i]);
		}

		removeResource(skyBoxVertexBuffer);

		for (auto& texture : skyBoxTextures)
		{
			removeResource(texture);
		}
			

		{
			removeShader(renderer, floorShader);
			removeResource(floorVertices);
			removeRasterizerState(floorRasterizerState);
			
			removeSampler(renderer, shadowMapSampler);
			removeRootSignature(renderer, floorRootSignature);

			removeResource(particleVertices);
			removeRasterizerState(particlesRasterizerState);

			removeDepthState(particlesDepthState);
			removeBlendState(particlesBlendState);

			removeSampler(renderer, particleImageSampler);
			removeResource(particlesTexture);
			removeShader(renderer, particlesShader);
			removeRootSignature(renderer, particlesRootSignature);

			removeShader(renderer, particlesShadowShader);

			for (auto particleInstancesData : particlesPerInstanceData)
			{
				removeResource(particleInstancesData);
			}
			
			for (auto particleInstancesData : particlesPerInstanceDataShadowMap)
			{
				removeResource(particleInstancesData);
			}

			for (auto buffer : shadowMapUniformsBuffers)
			{
				removeResource(buffer);
			}

			for (auto buffer : shadowReceiversUniformBuffer)
			{
				removeResource(buffer);
			}

			removeDepthState(depthStencilStateDisableAll);
		}

		removeSampler(renderer, skyBoxSampler);
		removeShader(renderer, skyBoxShader);
		removeRootSignature(renderer, skyBoxRootSignature);

		removeDepthState(depthTestEnabledState);
		removeRasterizerState(skyBoxRasterizerState);

		for (uint32_t i = 0; i < PRE_RENDERED_FRAMES_COUNT; ++i)
		{
			removeFence(renderer, frameCompleteFences[i]);
			removeSemaphore(renderer, frameCompleteSemaphores[i]);
		}
		removeSemaphore(renderer, imageAcquiredSemaphore);

		removeCmd_n(commandsPool, PRE_RENDERED_FRAMES_COUNT, commandsBuffers);
		removeCmdPool(renderer, commandsPool);

		removeResourceLoaderInterface(renderer);
		removeQueue(commandQueue);
		removeRenderer(renderer);
	}

	bool Load() override
	{
		if (!addSwapChain())
		{
			return false;
		}
			
		if (!addDepthBuffer())
		{
			return false;
		}
			
		if (!userInterface.Load(swapChain->ppSwapchainRenderTargets))
		{
			return false;
		}
			
		#if defined(NEED_JOYSTICK)
		if (!gVirtualJoystick.Load(pSwapChain->ppSwapchainRenderTargets[0], pDepthBuffer->mDesc.mFormat))
		{
			return false;
		}
		#endif

		const ClearValue maxClearValue {{1.0f, 1.0f, 1.0f, 1.0f}};

		RenderTargetDesc shadowMapDepthBufferDescription = {};
		shadowMapDepthBufferDescription.mArraySize = 1;
		shadowMapDepthBufferDescription.mClearValue = maxClearValue;
		shadowMapDepthBufferDescription.mDepth = 1;
		shadowMapDepthBufferDescription.mMipLevels = 1;
		shadowMapDepthBufferDescription.mFlags = TEXTURE_CREATION_FLAG_NONE;
		shadowMapDepthBufferDescription.mFormat = ImageFormat::R32F;
		shadowMapDepthBufferDescription.mWidth = SHADOW_MAP_WIDTH;
		shadowMapDepthBufferDescription.mHeight = SHADOW_MAP_HEIGHT;
		shadowMapDepthBufferDescription.mSampleCount = SAMPLE_COUNT_1;
		shadowMapDepthBufferDescription.mSampleQuality = 0;
		shadowMapDepthBufferDescription.pDebugName = L"Shadow Map Depth Render Target";

		addRenderTarget(renderer, &shadowMapDepthBufferDescription, &shadowMapDepth);

		RenderTargetDesc shadowMapColorBufferDescription = {};
		shadowMapColorBufferDescription.mArraySize = 1;
		shadowMapColorBufferDescription.mClearValue = maxClearValue;
		shadowMapColorBufferDescription.mDepth = 1;
		shadowMapColorBufferDescription.mFormat = ImageFormat::RGBA8;
		shadowMapColorBufferDescription.mWidth = shadowMapDepthBufferDescription.mWidth;
		shadowMapColorBufferDescription.mHeight = shadowMapDepthBufferDescription.mHeight;
		shadowMapColorBufferDescription.mSampleCount = SAMPLE_COUNT_1;
		shadowMapColorBufferDescription.mSampleQuality = 0;
		shadowMapColorBufferDescription.pDebugName = L"Shadow Map Color Render Target";

		addRenderTarget(renderer, &shadowMapColorBufferDescription, &shadowMapColor);

		//layout and pipeline for sphere draw
		VertexLayout vertexLayout = {};
		vertexLayout.mAttribCount = 2;
		vertexLayout.mAttribs[0].mSemantic = SEMANTIC_POSITION;
		vertexLayout.mAttribs[0].mFormat = ImageFormat::RGB32F;
		vertexLayout.mAttribs[0].mBinding = 0;
		vertexLayout.mAttribs[0].mLocation = 0;
		vertexLayout.mAttribs[0].mOffset = 0;
		vertexLayout.mAttribs[1].mSemantic = SEMANTIC_NORMAL;
		vertexLayout.mAttribs[1].mFormat = ImageFormat::RGB32F;
		vertexLayout.mAttribs[1].mBinding = 0;
		vertexLayout.mAttribs[1].mLocation = 1;
		vertexLayout.mAttribs[1].mOffset = 3 * sizeof(float);

		GraphicsPipelineDesc pipelineSettings = {};
		pipelineSettings.mPrimitiveTopo = PRIMITIVE_TOPO_TRI_LIST;
		pipelineSettings.mRenderTargetCount = 1;
		pipelineSettings.pDepthState = depthTestEnabledState;
		pipelineSettings.pColorFormats = &swapChain->ppSwapchainRenderTargets[0]->mDesc.mFormat;
		pipelineSettings.pSrgbValues = &swapChain->ppSwapchainRenderTargets[0]->mDesc.mSrgb;
		pipelineSettings.mSampleCount = swapChain->ppSwapchainRenderTargets[0]->mDesc.mSampleCount;
		pipelineSettings.mSampleQuality = swapChain->ppSwapchainRenderTargets[0]->mDesc.mSampleQuality;
		pipelineSettings.mDepthStencilFormat = depthBuffer->mDesc.mFormat;
		pipelineSettings.pVertexLayout = &vertexLayout;

		// floor pipeline
		vertexLayout = {};
		vertexLayout.mAttribCount = 1;
		vertexLayout.mAttribs[0].mSemantic = SEMANTIC_POSITION;
		vertexLayout.mAttribs[0].mFormat = ImageFormat::RGB32F;
		vertexLayout.mAttribs[0].mBinding = 0;
		vertexLayout.mAttribs[0].mLocation = 0;
		vertexLayout.mAttribs[0].mOffset = 0;
		pipelineSettings.pShaderProgram = floorShader;
		pipelineSettings.pRasterizerState = floorRasterizerState;
		pipelineSettings.pRootSignature = floorRootSignature;
		addPipeline(renderer, &pipelineSettings, &floorPipeline);

		// particle pipeline
		VertexLayout particleVertexLayout = {};
		particleVertexLayout.mAttribCount = 1;
		particleVertexLayout.mAttribs[0].mSemantic = SEMANTIC_POSITION;
		particleVertexLayout.mAttribs[0].mFormat = ImageFormat::RG32F;
		particleVertexLayout.mAttribs[0].mBinding = 0;
		particleVertexLayout.mAttribs[0].mLocation = 0;
		particleVertexLayout.mAttribs[0].mOffset = 0;
		pipelineSettings.pShaderProgram = particlesShader;
		pipelineSettings.pBlendState = particlesBlendState;
		pipelineSettings.pDepthState = particlesDepthState;
		pipelineSettings.pRasterizerState = particlesRasterizerState;
		pipelineSettings.pRootSignature = particlesRootSignature;
		pipelineSettings.pVertexLayout = &particleVertexLayout;
		addPipeline(renderer, &pipelineSettings, &particlesPipeline);

		//layout and pipeline for skybox draw
		vertexLayout = {};
		vertexLayout.mAttribCount = 1;
		vertexLayout.mAttribs[0].mSemantic = SEMANTIC_POSITION;
		vertexLayout.mAttribs[0].mFormat = ImageFormat::RGBA32F;
		vertexLayout.mAttribs[0].mBinding = 0;
		vertexLayout.mAttribs[0].mLocation = 0;
		vertexLayout.mAttribs[0].mOffset = 0;
		pipelineSettings.pBlendState = nullptr;
		pipelineSettings.pDepthState = nullptr;
		pipelineSettings.pRasterizerState = skyBoxRasterizerState;
		pipelineSettings.pShaderProgram = skyBoxShader;
		pipelineSettings.pRootSignature = skyBoxRootSignature;
		pipelineSettings.pVertexLayout = &vertexLayout;
		addPipeline(renderer, &pipelineSettings, &skyBoxPipeline);

		GraphicsPipelineDesc shadowMapPipelineSettings = pipelineSettings;
		shadowMapPipelineSettings.mRenderTargetCount = SHADOW_MAP_BUFFERS_COUNT;
		shadowMapPipelineSettings.pDepthState = depthStencilStateDisableAll;
		shadowMapPipelineSettings.mDepthStencilFormat = ImageFormat::NONE;
		
		ImageFormat::Enum shadowMapColorFormats[SHADOW_MAP_BUFFERS_COUNT] = { shadowMapColor->mDesc.mFormat, shadowMapDepth->mDesc.mFormat };
		shadowMapPipelineSettings.pColorFormats = shadowMapColorFormats;

		bool shadowMapSrgbValues[SHADOW_MAP_BUFFERS_COUNT] = { shadowMapColor->mDesc.mSrgb, shadowMapDepth->mDesc.mSrgb };
		shadowMapPipelineSettings.pSrgbValues = shadowMapSrgbValues;
		
		shadowMapPipelineSettings.mSampleCount = SAMPLE_COUNT_1;
		shadowMapPipelineSettings.mSampleQuality = 0;
		shadowMapPipelineSettings.pRootSignature = particlesRootSignature;
		shadowMapPipelineSettings.pRasterizerState = particlesRasterizerState;
		shadowMapPipelineSettings.pShaderProgram = particlesShadowShader;
		shadowMapPipelineSettings.pVertexLayout = &particleVertexLayout;
		shadowMapPipelineSettings.pBlendState = particlesBlendState;
		addPipeline(renderer, &shadowMapPipelineSettings, &shadowMapPassPipeline);

		return true;
	}

	void Unload() override
	{
		waitForFences(commandQueue, PRE_RENDERED_FRAMES_COUNT, frameCompleteFences, /*signel: */true);

		userInterface.Unload();

		#if defined(NEED_JOYSTICK)
		gVirtualJoystick.Unload();
		#endif

		removePipeline(renderer, skyBoxPipeline);
		
		removePipeline(renderer, floorPipeline);
		removePipeline(renderer, particlesPipeline);
		
		removePipeline(renderer, shadowMapPassPipeline);

		removeRenderTarget(renderer, shadowMapDepth);
		removeRenderTarget(renderer, shadowMapColor);

		removeSwapChain(renderer, swapChain);
		removeRenderTarget(renderer, depthBuffer);
	}

	void Update(const float deltaTime) override
	{
		if (getKeyDown(KEY_BUTTON_X))
		{
			recenterCameraView(170.0f);
		}

		cameraController->update(deltaTime);
		
		mat4 cameraView = cameraController->getViewMatrix();

		const float aspectInverse = static_cast<float>(mSettings.mHeight) / static_cast<float>(mSettings.mWidth);
		const float horizontal_fov = PI / 2.0f;
		const float zNear = 0.1f;
		const float zFar = 1000.0f;

		const mat4 projection = mat4::perspective(horizontal_fov, aspectInverse, zNear, zFar);
		commonUniformData.mProjectView = projection * cameraView;
		commonUniformData.mCamera = cameraView;
		commonUniformData.zProjection.setX(zFar / (zFar - zNear));
		commonUniformData.zProjection.setY(- zFar * zNear / (zFar - zNear));
		commonUniformData.zProjection.setZ(0);
		commonUniformData.zProjection.setW(0);
		commonUniformData.mLightPosition = vec3(0, 0, 0);
		commonUniformData.mLightColor = vec3(0.9f, 0.9f, 0.7f); // Pale Yellow

		emitter.update(deltaTime);

		const auto particlesComponentMultiplier = sizeof(float) * emitter.getAliveParticlesCount();
		ASSERT(emitter.getAliveParticlesCount() <= MAX_PARTICLES_COUNT);

		emitter.sort(cameraView);
		::memcpy(particlesFinalRender.positions, emitter.getPositions(), particlesComponentMultiplier * CONST_BUFFER_QUANT_SIZE);
		::memcpy(particlesFinalRender.timeAndStyle, emitter.getBehaviors(), particlesComponentMultiplier * CONST_BUFFER_QUANT_SIZE);

		emitter.sort(lightView);
		ASSERT(emitter.getAliveParticlesCount() <= MAX_PARTICLES_COUNT);
		::memcpy(particlesShadowMap.positions, emitter.getPositions(), particlesComponentMultiplier * CONST_BUFFER_QUANT_SIZE);
		::memcpy(particlesShadowMap.timeAndStyle, emitter.getBehaviors(), particlesComponentMultiplier * CONST_BUFFER_QUANT_SIZE);

		shadowMapUniforms.mCamera = lightView;
		shadowMapUniforms.mProjectView = lightProjection * lightView;

		shadowReceiversUniforms.shadowMapCamera = lightView;
		shadowReceiversUniforms.shadowMapMvp = lightProjection * lightView;

		cameraView.setTranslation(vec3(0));
		skyBoxUniformData = commonUniformData;
		skyBoxUniformData.mProjectView = projection * cameraView;
		skyBoxUniformData.mCamera = cameraView;
		skyBoxUniformData.zProjection.setX(zFar / (zFar - zNear));
		skyBoxUniformData.zProjection.setY(-zFar * zNear / (zFar - zNear));
		skyBoxUniformData.zProjection.setZ(0);
		skyBoxUniformData.zProjection.setW(0);
	}

	void Draw() override
	{
		acquireNextImage(renderer, swapChain, imageAcquiredSemaphore, /*p_fence: */nullptr, &preRenderedFrameIndex);
		ASSERT(0 <= preRenderedFrameIndex && PRE_RENDERED_FRAMES_COUNT > preRenderedFrameIndex);

		RenderTarget* pRenderTarget = swapChain->ppSwapchainRenderTargets[preRenderedFrameIndex];
		Semaphore* pRenderCompleteSemaphore = frameCompleteSemaphores[preRenderedFrameIndex];
		Fence* pRenderCompleteFence = frameCompleteFences[preRenderedFrameIndex];

		// Stall if CPU is running "Swap Chain Buffer Count" frames ahead of GPU
		FenceStatus fenceStatus;
		getFenceStatus(renderer, pRenderCompleteFence, &fenceStatus);
		if (fenceStatus == FENCE_STATUS_INCOMPLETE)
		{
			waitForFences(commandQueue, 1, &pRenderCompleteFence, false);
		}
			
		BufferUpdateDesc viewProjCbv = { commonUniformBuffers[preRenderedFrameIndex], &commonUniformData };
		updateResource(&viewProjCbv);

		BufferUpdateDesc skyboxViewProjCbv = { skyboxUniformBuffers[preRenderedFrameIndex], &skyBoxUniformData };
		updateResource(&skyboxViewProjCbv);

		BufferUpdateDesc particlesUpdateDescription = { particlesPerInstanceData[preRenderedFrameIndex], &particlesFinalRender };
		updateResource(&particlesUpdateDescription);

		BufferUpdateDesc particlesShadowMapUpdateDescription = { particlesPerInstanceDataShadowMap[preRenderedFrameIndex], &particlesShadowMap };
		updateResource(&particlesShadowMapUpdateDescription);

		BufferUpdateDesc particlesShadowUniformsDescription = { shadowMapUniformsBuffers[preRenderedFrameIndex], &shadowMapUniforms };
		updateResource(&particlesShadowUniformsDescription);

		BufferUpdateDesc shadowReceiversUniformsDescription = { shadowReceiversUniformBuffer[preRenderedFrameIndex], &shadowReceiversUniforms };
		updateResource(&shadowReceiversUniformsDescription);

		// simply record the screen cleaning command
		LoadActionsDesc loadActions = {};
		loadActions.mLoadActionsColor[0] = LOAD_ACTION_CLEAR;
		loadActions.mClearColorValues[0].r = 1.0f;
		loadActions.mClearColorValues[0].g = 1.0f;
		loadActions.mClearColorValues[0].b = 0.0f;
		loadActions.mClearColorValues[0].a = 0.0f;
		loadActions.mLoadActionDepth = LOAD_ACTION_CLEAR;
		loadActions.mClearDepth.depth = 1.0f;
		loadActions.mClearDepth.stencil = 0;
		
		Cmd* cmd = commandsBuffers[preRenderedFrameIndex];
		beginCmd(cmd);

		const int MAX_DESCRIPTORS_COUNT = 8;

		cmdBeginDebugMarker(cmd, 0.5f, 0.5f, 0, "Draw particles shadow map");
		{
			LoadActionsDesc shadowMapLoadActions = {};
			for (int i = 0; i < SHADOW_MAP_BUFFERS_COUNT; ++i)
			{
				shadowMapLoadActions.mLoadActionsColor[i] = LOAD_ACTION_CLEAR;
				shadowMapLoadActions.mClearColorValues[i].r = 1.0f;
				shadowMapLoadActions.mClearColorValues[i].g = 1.0f;
				shadowMapLoadActions.mClearColorValues[i].b = 1.0f;
				shadowMapLoadActions.mClearColorValues[i].a = 1.0f;
			}
			shadowMapLoadActions.mLoadActionDepth = LOAD_ACTION_DONTCARE;

			TextureBarrier barrier[SHADOW_MAP_BUFFERS_COUNT] =
			{
				{ shadowMapDepth->pTexture, RESOURCE_STATE_RENDER_TARGET }, 
				{ shadowMapColor->pTexture, RESOURCE_STATE_RENDER_TARGET } 
			};
			cmdResourceBarrier(cmd, 0, nullptr, _countof(barrier), barrier, false);

			RenderTarget* renderTargets[SHADOW_MAP_BUFFERS_COUNT] = { shadowMapColor, shadowMapDepth, };
			cmdBindRenderTargets(cmd, _countof(renderTargets), renderTargets, nullptr, &shadowMapLoadActions, nullptr, nullptr, -1, -1);
			cmdSetViewport(cmd, 0.0f, 0.0f, static_cast<float>(shadowMapDepth->mDesc.mWidth), static_cast<float>(shadowMapDepth->mDesc.mHeight), 0.0f, 1.0f);
			cmdSetScissor(cmd, 0, 0, shadowMapDepth->mDesc.mWidth, shadowMapDepth->mDesc.mHeight);

			DescriptorData descriptors[MAX_DESCRIPTORS_COUNT] = {};
			int descriptorCount = 0;
			descriptors[descriptorCount].pName = "uniformBlock";
			descriptors[descriptorCount++].ppBuffers = &shadowReceiversUniformBuffer[preRenderedFrameIndex];
			descriptors[descriptorCount].pName = "particlesInstances";
			descriptors[descriptorCount++].ppBuffers = &particlesPerInstanceDataShadowMap[preRenderedFrameIndex];
			descriptors[descriptorCount].pName = "image";
			descriptors[descriptorCount++].ppTextures = &particlesTexture;

			cmdBindPipeline(cmd, shadowMapPassPipeline);
			
			cmdBindDescriptors(cmd, particlesRootSignature, descriptorCount, descriptors);
			cmdBindVertexBuffer(cmd, 1, &particleVertices, nullptr);
			cmdDrawInstanced(cmd, particleVertexCount, 0, emitter.getAliveParticlesCount(), 0);
		}
		cmdEndDebugMarker(cmd);
		
		{
		TextureBarrier barriers[] = 
		{
			{ pRenderTarget->pTexture, RESOURCE_STATE_RENDER_TARGET },
			{ depthBuffer->pTexture, RESOURCE_STATE_DEPTH_WRITE },
		};
		cmdResourceBarrier(cmd, 0, nullptr, _countof(barriers), barriers, false);
		}

		cmdBindRenderTargets(cmd, 1, &pRenderTarget, depthBuffer, &loadActions, nullptr, nullptr, -1, -1);
		cmdSetViewport(cmd, 0.0f, 0.0f, static_cast<float>(pRenderTarget->mDesc.mWidth), static_cast<float>(pRenderTarget->mDesc.mHeight), 0.0f, 1.0f);
		cmdSetScissor(cmd, 0, 0, pRenderTarget->mDesc.mWidth, pRenderTarget->mDesc.mHeight);

		cmdBeginDebugMarker(cmd, 0, 0, 1, "Draw skybox");
		{
			cmdBindPipeline(cmd, skyBoxPipeline);

			DescriptorData params[MAX_DESCRIPTORS_COUNT] = {};
			auto descriptorCount = 0u;

			params[descriptorCount].pName = "uniformBlock";
			params[descriptorCount++].ppBuffers = &skyboxUniformBuffers[preRenderedFrameIndex];
			params[descriptorCount].pName = "RightText";
			params[descriptorCount++].ppTextures = &skyBoxTextures[0];
			params[descriptorCount].pName = "LeftText";
			params[descriptorCount++].ppTextures = &skyBoxTextures[1];
			params[descriptorCount].pName = "TopText";
			params[descriptorCount++].ppTextures = &skyBoxTextures[2];
			params[descriptorCount].pName = "BotText";
			params[descriptorCount++].ppTextures = &skyBoxTextures[3];
			params[descriptorCount].pName = "FrontText";
			params[descriptorCount++].ppTextures = &skyBoxTextures[4];
			params[descriptorCount].pName = "BackText";
			params[descriptorCount++].ppTextures = &skyBoxTextures[5];

			ASSERT(descriptorCount <= _countof(params));

			cmdBindDescriptors(cmd, skyBoxRootSignature, descriptorCount, params);
			cmdBindVertexBuffer(cmd, 1, &skyBoxVertexBuffer, NULL);
			cmdDraw(cmd, 36, 0);
		}
		cmdEndDebugMarker(cmd);

		{
			//

			{
			DescriptorData floorDrawParameters[MAX_DESCRIPTORS_COUNT] = {};
			
			auto descriptorCount = 0u;

			floorDrawParameters[descriptorCount].pName = "uniformBlock";
			floorDrawParameters[descriptorCount++].ppBuffers = &commonUniformBuffers[preRenderedFrameIndex];
			
			floorDrawParameters[descriptorCount].pName = "shadowReceiverUniforms";
			floorDrawParameters[descriptorCount++].ppBuffers = &shadowReceiversUniformBuffer[preRenderedFrameIndex];

			floorDrawParameters[descriptorCount].pName = "shadowMapDepth";
			floorDrawParameters[descriptorCount++].ppTextures = &shadowMapDepth->pTexture;

			floorDrawParameters[descriptorCount].pName = "shadowMapColor";
			floorDrawParameters[descriptorCount++].ppTextures = &shadowMapColor->pTexture;

			ASSERT(descriptorCount <= _countof(floorDrawParameters));

			cmdBeginDebugMarker(cmd, 1, 1, 1, "Draw floor");

				TextureBarrier shadowMapBarrier[SHADOW_MAP_BUFFERS_COUNT] =
				{
					{ shadowMapDepth->pTexture, RESOURCE_STATE_SHADER_RESOURCE },
					{ shadowMapColor->pTexture, RESOURCE_STATE_SHADER_RESOURCE },
				};
				cmdResourceBarrier(cmd, 0, nullptr, _countof(shadowMapBarrier), shadowMapBarrier,  /*batch: */false);

				cmdBindPipeline(cmd, floorPipeline);
				
				cmdBindDescriptors(cmd, floorRootSignature, descriptorCount, floorDrawParameters);
				cmdBindVertexBuffer(cmd, /*buffer count: */1, &floorVertices,  /*offsets: */nullptr);

				cmdDraw(cmd, FLOOR_VERTEX_COUNT, /*first vertex: */0);

			cmdEndDebugMarker(cmd);
			}

			//

			cmdBeginDebugMarker(cmd, 0.5f, 0.5f, 1, "Draw particles");
			{
				DescriptorData particlesDescriptors[MAX_DESCRIPTORS_COUNT] = {};
				auto descriptorCount = 0u;

				particlesDescriptors[descriptorCount].pName = "uniformBlock";
				particlesDescriptors[descriptorCount++].ppBuffers = &commonUniformBuffers[preRenderedFrameIndex];

				particlesDescriptors[descriptorCount].pName = "image";
				particlesDescriptors[descriptorCount++].ppTextures = &particlesTexture;

				particlesDescriptors[descriptorCount].pName = "depthBuffer";
				particlesDescriptors[descriptorCount++].ppTextures = &depthBuffer->pTexture;

				particlesDescriptors[descriptorCount].pName = "particlesInstances";
				particlesDescriptors[descriptorCount++].ppBuffers = &particlesPerInstanceData[preRenderedFrameIndex];

				ASSERT(descriptorCount <= _countof(particlesDescriptors));

				cmdBindPipeline(cmd, particlesPipeline);
				TextureBarrier zReadBarrier[] =
				{
					{ pRenderTarget->pTexture, RESOURCE_STATE_RENDER_TARGET },
					{ depthBuffer->pTexture, RESOURCE_STATE_DEPTH_READ | RESOURCE_STATE_SHADER_RESOURCE },
				};
				cmdResourceBarrier(cmd, 0, nullptr, _countof(zReadBarrier), zReadBarrier, /*batch: */false);
				cmdBindDescriptors(cmd, particlesRootSignature, descriptorCount, particlesDescriptors);
				cmdBindVertexBuffer(cmd, 1, &particleVertices,  /*offsets: */nullptr);
				cmdDrawInstanced(cmd, particleVertexCount,  /*first vertex: */0, emitter.getAliveParticlesCount(),  /*first instance: */0);
			}
			cmdEndDebugMarker(cmd);
		}

		cmdBeginDebugMarker(cmd, 0, 1, 0, "Draw UI");
		static HiresTimer gTimer;
		gTimer.GetUSec(true);

		#if defined(NEED_JOYSTICK)
		gVirtualJoystick.Draw(cmd, pCameraController, { 1.0f, 1.0f, 1.0f, 1.0f });
		#endif

		drawDebugText(cmd, 8, 15, tinystl::string::format("CPU %f ms", gTimer.GetUSecAverage() / 1000.0f), &gFrameTimeDraw);
		userInterface.Draw(cmd);
		cmdBindRenderTargets(cmd, 0, nullptr, nullptr, nullptr, nullptr, nullptr, -1, -1);
		cmdEndDebugMarker(cmd);

		{
		TextureBarrier barriers[] = { { pRenderTarget->pTexture, RESOURCE_STATE_PRESENT } };
		cmdResourceBarrier(cmd, 0, nullptr, _countof(barriers), barriers, true);
		}
		endCmd(cmd);

		queueSubmit(commandQueue, 1, &cmd, pRenderCompleteFence, 1, &imageAcquiredSemaphore, 1, &pRenderCompleteSemaphore);
		queuePresent(commandQueue, swapChain, preRenderedFrameIndex, 1, &pRenderCompleteSemaphore);
	}

	tinystl::string GetName() override
	{
		return "00_Particles";
	}

	bool addSwapChain()
	{
		SwapChainDesc swapChainDescription = {};
		swapChainDescription.pWindow = pWindow;
		swapChainDescription.mPresentQueueCount = 1;
		swapChainDescription.ppPresentQueues = &commandQueue;
		swapChainDescription.mWidth = mSettings.mWidth;
		swapChainDescription.mHeight = mSettings.mHeight;
		swapChainDescription.mImageCount = PRE_RENDERED_FRAMES_COUNT;
		swapChainDescription.mSampleCount = SAMPLE_COUNT_1;
		swapChainDescription.mColorFormat = getRecommendedSwapchainFormat(true);
		swapChainDescription.mEnableVsync = false;
		::addSwapChain(renderer, &swapChainDescription, &swapChain);

		return nullptr != swapChain;
	}

	bool addDepthBuffer()
	{
		RenderTargetDesc depthRenderTargetDescription = {};
		depthRenderTargetDescription.mArraySize = 1;
		depthRenderTargetDescription.mClearValue.depth = 1.0f;
		depthRenderTargetDescription.mClearValue.stencil = 0;
		depthRenderTargetDescription.mDepth = 1;
		depthRenderTargetDescription.mFormat = ImageFormat::D32F;
		depthRenderTargetDescription.mHeight = mSettings.mHeight;
		depthRenderTargetDescription.mSampleCount = SAMPLE_COUNT_1;
		depthRenderTargetDescription.mSampleQuality = 0;
		depthRenderTargetDescription.mWidth = mSettings.mWidth;
		addRenderTarget(renderer, &depthRenderTargetDescription, &depthBuffer);

		return nullptr != depthBuffer;
	}

	void recenterCameraView(const float maxDistance)
	{
		const vec3 lookAt = vec3{0};

		vec3 p = cameraController->getViewPosition();
		vec3 d = p - lookAt;

		const float viewDirectionLengthSqr = lengthSqr(d);
		if (viewDirectionLengthSqr > (maxDistance * maxDistance))
		{
			d *= (maxDistance / sqrtf(viewDirectionLengthSqr));
		}

		p = d + lookAt;
		cameraController->moveTo(p);
		cameraController->lookAt(lookAt);
	}

	static bool cameraInputEvent(const ButtonData* const data)
	{
		cameraController->onInputEvent(data);
		return true;
	}

};

DEFINE_APPLICATION_MAIN(Particles)