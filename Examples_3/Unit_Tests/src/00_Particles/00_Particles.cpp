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

#include "../../../../Middleware_3/UI/AppUI.h"
#include "../../../../Middleware_3/Input/InputSystem.h"
#include "../../../../Middleware_3/Input/InputMappings.h"

#include "../../../../Common_3/ThirdParty/OpenSource/TinySTL/vector.h"
#include "../../../../Common_3/ThirdParty/OpenSource/TinySTL/string.h"

#if defined(TARGET_IOS) || defined(__ANDROID__)
#define NEED_JOYSTICK
#endif

struct ProjectionUniforms final
{
	mat4 mProjectView;
	mat4 mCamera;
	vec4 zProjection;
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

const auto SKY_BOX_FACES_COUNT = 6;

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

ICameraController* cameraController = nullptr;

class Particles final : public IApp
{
private:

	Renderer* renderer = nullptr;

	Queue* commandQueue = nullptr;
	CmdPool* commandsPool = nullptr;
	Cmd** commandsBuffers = nullptr;

	SwapChain* swapChain = nullptr;
	Fence* frameCompleteFences[PRE_RENDERED_FRAMES_COUNT] = { nullptr };
	Semaphore* imageAcquiredSemaphore = nullptr;
	Semaphore* frameCompleteSemaphores[PRE_RENDERED_FRAMES_COUNT] = { nullptr };

	Shader* skyBoxShader = nullptr;
	Buffer* skyBoxVertexBuffer = nullptr;
	Pipeline* skyBoxPipeline = nullptr;
	RootSignature* skyBoxRootSignature = nullptr;
	Sampler* skyBoxSampler = nullptr;
	Texture* skyBoxTextures[SKY_BOX_FACES_COUNT] = { nullptr };

	DepthState* depthTestEnabledState = nullptr;
	RasterizerState* skyBoxRasterizerState = nullptr;

	Buffer* cameraViewProjectionUniformBuffers[PRE_RENDERED_FRAMES_COUNT] = { nullptr };
	Buffer* skyBoxUniformBuffers[PRE_RENDERED_FRAMES_COUNT] = { nullptr };

	uint32_t preRenderedFrameIndex = 0;

	ProjectionUniforms commonUniformData;
	ProjectionUniforms skyBoxUniformData;

	#if defined(NEED_JOYSTICK)
	VirtualJoystickUI gVirtualJoystick;
	#endif

	TextDrawDesc frameTimeDraw{/*font: */0, /*color: */0xff000000, /*size: */18};
	UIApp userInterface;

	LogManager logManager;

	static const int FLOOR_VERTEX_COUNT = 6;
	static const int SHADOW_MAP_WIDTH = 1024;
	static const int SHADOW_MAP_HEIGHT = 1024;

	RenderTarget* depthBuffer = nullptr;
	RenderTarget* shadowMapDepthBuffer = nullptr;

	Shader* floorShader = nullptr;
	Shader* floorDepthOnlyShader = nullptr;
	Buffer* floorVertices = nullptr;
	Pipeline* floorPipeline = nullptr;
	Pipeline* floorShadowMapDepthOnlyPassPipeline = nullptr;
	BlendState* noColorWriteBlendState = nullptr;
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
	Pipeline* shadowMapParticlesPassPipeline = nullptr;
	Shader* particlesShadowShader = nullptr;

	mat4 lightView;
	mat4 lightProjection;
	ProjectionUniforms shadowMapUniforms;
	ShadowReceiverUniform shadowReceiversUniforms;
	Buffer* shadowMapUniformsBuffers[PRE_RENDERED_FRAMES_COUNT] = { nullptr };
	Emitter emitter;

	ParticlesUniform particlesFinalRender;
	ParticlesUniform particlesShadowMap;

	Buffer* particlesPerInstanceData[PRE_RENDERED_FRAMES_COUNT] = { nullptr };
	Buffer* particlesPerInstanceDataShadowMap[PRE_RENDERED_FRAMES_COUNT] = { nullptr };

	void prepareFloorShaders()
	{
		{
		ShaderLoadDesc floorShaderSource = {};
		floorShaderSource.mStages[0] = { "floor.vert", nullptr, 0, FSR_SrcShaders };
		floorShaderSource.mStages[1] = { "floor_color_render.frag", nullptr, 0, FSR_SrcShaders };
		addShader(renderer, &floorShaderSource, &floorShader);
		}

		{
		ShaderLoadDesc floorDepthOnlyShaderSource = {};
		floorDepthOnlyShaderSource.mStages[0] = { "floor.vert", nullptr, 0, FSR_SrcShaders };
		floorDepthOnlyShaderSource.mStages[1] = { "floor_z_only_pass.frag", nullptr, 0, FSR_SrcShaders };
		addShader(renderer, &floorDepthOnlyShaderSource, &floorDepthOnlyShader);
		}
	}

	void prepareFloorVertexBuffer()
	{
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
	}

	void prepareFloorRootSignature()
	{
		ASSERT(nullptr != floorShader);
		ASSERT(nullptr != floorDepthOnlyShader);
		ASSERT(nullptr != shadowMapSampler);

		Shader* shadowReceiversShaders[] = { floorShader, floorDepthOnlyShader };
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

	void prepareFloorRasterizerState()
	{
		RasterizerStateDesc floorRasterizerStateDescription = {};
		floorRasterizerStateDescription.mCullMode = CULL_MODE_FRONT;
		addRasterizerState(renderer, &floorRasterizerStateDescription, &floorRasterizerState);
	}

	void prepareFloorShadowMapStuff()
	{
		{
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
		}

		{
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

		{
		BlendStateDesc shadowMapPassFloorBlendStateDescription = {};
		shadowMapPassFloorBlendStateDescription.mIndependentBlend = false;
		shadowMapPassFloorBlendStateDescription.mAlphaToCoverage = false;
		shadowMapPassFloorBlendStateDescription.mRenderTargetMask = BLEND_STATE_TARGET_0;

		shadowMapPassFloorBlendStateDescription.mMasks[0] = NONE;
		shadowMapPassFloorBlendStateDescription.mBlendAlphaModes[0] = BM_ADD;
		shadowMapPassFloorBlendStateDescription.mBlendModes[0] = BM_ADD;
		shadowMapPassFloorBlendStateDescription.mDstAlphaFactors[0] = BC_ZERO;
		shadowMapPassFloorBlendStateDescription.mDstFactors[0] = BC_ZERO;
		shadowMapPassFloorBlendStateDescription.mSrcAlphaFactors[0] = BC_ONE;
		shadowMapPassFloorBlendStateDescription.mSrcFactors[0] = BC_ONE;

		addBlendState(renderer, &shadowMapPassFloorBlendStateDescription, &noColorWriteBlendState);
		}
	}

	void prepareFloorResources()
	{
		prepareFloorShaders();
		prepareFloorVertexBuffer();
		prepareFloorRasterizerState();
		prepareFloorShadowMapStuff();
		prepareFloorRootSignature();
	}

	void prepareParticlesShaders()
	{
		{
		ShaderLoadDesc particlesShaderSource = {};
		particlesShaderSource.mStages[0] = { "particle.vert", nullptr, 0, FSR_SrcShaders };
		particlesShaderSource.mStages[1] = { "particle.frag", nullptr, 0, FSR_SrcShaders };
		addShader(renderer, &particlesShaderSource, &particlesShader);
		}

		{
		ShaderLoadDesc particlesShadowDepthColorShaderSource = {};
		particlesShadowDepthColorShaderSource.mStages[0] = { "particle_shadow_map.vert", nullptr, 0, FSR_SrcShaders };
		particlesShadowDepthColorShaderSource.mStages[1] = { "particle_shadow_map.frag", nullptr, 0, FSR_SrcShaders };
		addShader(renderer, &particlesShadowDepthColorShaderSource, &particlesShadowShader);
		}
	}

	void prepareParticlesVertexBuffer()
	{
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
	}

	void prepareParticlesBlendState()
	{
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
	}

	void prepareParticlesDepthState()
	{
		DepthStateDesc depthStateDescription {};
		depthStateDescription.mDepthTest = true;
		depthStateDescription.mDepthWrite = false;
		depthStateDescription.mDepthFunc = CMP_LEQUAL;
		addDepthState(renderer, &depthStateDescription, &particlesDepthState);
	}

	void loadParticlesTexture()
	{
		TextureLoadDesc textureDescription = {};
		textureDescription.mRoot = FSR_Textures;
		textureDescription.mUseMipmaps = true;
		textureDescription.pFilename = "blackSmoke00.png";
		textureDescription.ppTexture = &particlesTexture;
		addResource(&textureDescription, true);
	}

	void prepareParticlesRasterizerState()
	{
		RasterizerStateDesc rasterizerStateDescription = {};
		rasterizerStateDescription.mCullMode = CULL_MODE_NONE;
		addRasterizerState(renderer, &rasterizerStateDescription, &particlesRasterizerState);
	}

	void prepareSamplerForParticles()
	{
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
	}

	void prepareParticlesRootSignature()
	{
		ASSERT(nullptr != particlesShader);
		ASSERT(nullptr != particlesShadowShader);
		ASSERT(nullptr != particleImageSampler);

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

	void prepareParticlesPseudoInstancingUniformBuffers()
	{
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
	}

	void prepareParticlesStyleData()
	{
		particlesFinalRender.particlesLifeLength = Emitter::LIFE_LENGTH_SECONDS;
		particlesShadowMap.particlesLifeLength = Emitter::LIFE_LENGTH_SECONDS;

		auto styleCounter = 0;

		particlesFinalRender.colorAndSizeScale[styleCounter] = vec4{1.0f, 0.0f, 0.0f, 0.5f};
		particlesShadowMap.colorAndSizeScale[styleCounter++] = vec4{1.0f, 0.0f, 0.0f, 0.5f};

		particlesFinalRender.colorAndSizeScale[styleCounter] = vec4{1.0f, 0.0f, 0.0f, 0.8f};
		particlesShadowMap.colorAndSizeScale[styleCounter++] = vec4{1.0f, 0.0f, 0.0f, 0.8f};

		particlesFinalRender.colorAndSizeScale[styleCounter] = vec4{0.0f, 0.0f, 1.0f, 1.0f};
		particlesShadowMap.colorAndSizeScale[styleCounter++] = vec4{0.0f, 0.0f, 1.0f, 1.0f};
		
		ASSERT(styleCounter == _countof(particlesShadowMap.colorAndSizeScale));
		ASSERT(styleCounter == _countof(particlesFinalRender.colorAndSizeScale));
	}

	void prepareParticlesShadowMapStuff()
	{
		BufferLoadDesc shadowMapUniformsDescription = {};
		shadowMapUniformsDescription.mDesc.mDescriptors = DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		shadowMapUniformsDescription.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
		shadowMapUniformsDescription.mDesc.mSize = sizeof(ProjectionUniforms);
		shadowMapUniformsDescription.mDesc.mFlags = BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
		shadowMapUniformsDescription.pData = nullptr;
		for (auto& buffer : shadowMapUniformsBuffers)
		{
			shadowMapUniformsDescription.ppBuffer = &buffer;
			addResource(&shadowMapUniformsDescription);
		}
	}

	void prepareParticlesResources()
	{
		prepareParticlesShaders();
		prepareParticlesVertexBuffer();
		prepareParticlesDepthState();
		prepareParticlesBlendState();
		loadParticlesTexture();
		prepareParticlesRasterizerState();
		prepareSamplerForParticles();
		prepareParticlesRootSignature();
		prepareParticlesPseudoInstancingUniformBuffers();
		prepareParticlesStyleData();
		prepareParticlesShadowMapStuff();
	}

	static constexpr float SHADOW_MAP_Z_NEAR = 0.1f;
	static constexpr float SHADOW_MAP_Z_FAR = 100.0f;

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
		
		lightProjection = mat4::perspective(PI / 2.0f, SHADOW_MAP_HEIGHT / static_cast<float>(SHADOW_MAP_WIDTH), SHADOW_MAP_Z_NEAR, SHADOW_MAP_Z_FAR);
	}

	bool Init() override
	{
		RendererDesc settings = {};
		
		initRenderer(GetName(), &settings, &renderer);
		
		if (!renderer)
		{
			return false;
		}
			
		QueueDesc commandsQueueDescription = {};
		commandsQueueDescription.mType = CMD_POOL_DIRECT;
		addQueue(renderer, &commandsQueueDescription, &commandQueue);
		addCmdPool(renderer, commandQueue, false, &commandsPool);
		addCmd_n(commandsPool, false, PRE_RENDERED_FRAMES_COUNT, &commandsBuffers);

		ASSERT(PRE_RENDERED_FRAMES_COUNT  == _countof(frameCompleteFences));
		ASSERT(PRE_RENDERED_FRAMES_COUNT  == _countof(frameCompleteSemaphores));
		for (uint32_t i = 0; i < PRE_RENDERED_FRAMES_COUNT; ++i)
		{
			addFence(renderer, &frameCompleteFences[i]);
			addSemaphore(renderer, &frameCompleteSemaphores[i]);
		}
		addSemaphore(renderer, &imageAcquiredSemaphore);

		initResourceLoaderInterface(renderer, DEFAULT_MEMORY_BUDGET, true);
		initDebugRendererInterface(renderer, "TitilliumText/TitilliumText-Bold.otf", FSR_Builtin_Fonts);

		#if defined(NEED_JOYSTICK)
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
		skyShader.mStages[0] = { "skybox.vert", nullptr, 0, FSR_SrcShaders };
		skyShader.mStages[1] = { "skybox.frag", nullptr, 0, FSR_SrcShaders };
		addShader(renderer, &skyShader, &skyBoxShader);

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

		BufferLoadDesc commonUniformBufferDescription = {};
		commonUniformBufferDescription.mDesc.mDescriptors = DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		commonUniformBufferDescription.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
		commonUniformBufferDescription.mDesc.mSize = sizeof(ProjectionUniforms);
		commonUniformBufferDescription.mDesc.mFlags = BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
		commonUniformBufferDescription.pData = nullptr;
		
		ASSERT(PRE_RENDERED_FRAMES_COUNT == _countof(cameraViewProjectionUniformBuffers));
		ASSERT(PRE_RENDERED_FRAMES_COUNT == _countof(skyBoxUniformBuffers));
		for (uint32_t i = 0; i < PRE_RENDERED_FRAMES_COUNT; ++i)
		{
			commonUniformBufferDescription.ppBuffer = &cameraViewProjectionUniformBuffers[i];
			addResource(&commonUniformBufferDescription);
			commonUniformBufferDescription.ppBuffer = &skyBoxUniformBuffers[i];
			addResource(&commonUniformBufferDescription);
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
		waitForFences(commandQueue, /*fence count: */1, &frameCompleteFences[preRenderedFrameIndex], /*signal: */true);

		destroyCameraController(cameraController);

		removeDebugRendererInterface();

		#if defined(NEED_JOYSTICK)
		gVirtualJoystick.Exit();
		#endif

		userInterface.Exit();

		ASSERT(PRE_RENDERED_FRAMES_COUNT == _countof(cameraViewProjectionUniformBuffers));
		ASSERT(PRE_RENDERED_FRAMES_COUNT == _countof(skyBoxUniformBuffers));
		for (uint32_t i = 0; i < PRE_RENDERED_FRAMES_COUNT; ++i)
		{
			removeResource(cameraViewProjectionUniformBuffers[i]);
			removeResource(skyBoxUniformBuffers[i]);
		}

		removeResource(skyBoxVertexBuffer);

		for (auto& texture : skyBoxTextures)
		{
			removeResource(texture);
		}
			

		{
			removeShader(renderer, floorShader);
			removeShader(renderer, floorDepthOnlyShader);
			removeResource(floorVertices);
			removeBlendState(noColorWriteBlendState);
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
		}

		removeSampler(renderer, skyBoxSampler);
		removeShader(renderer, skyBoxShader);
		removeRootSignature(renderer, skyBoxRootSignature);

		removeDepthState(depthTestEnabledState);
		removeRasterizerState(skyBoxRasterizerState);

		for (auto i = 0u; i < PRE_RENDERED_FRAMES_COUNT; ++i)
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
			
		if (!addDepthBuffers())
		{
			return false;
		}
			
		if (!userInterface.Load(swapChain->ppSwapchainRenderTargets))
		{
			return false;
		}
			
		#if defined(NEED_JOYSTICK)
		if (!gVirtualJoystick.Load(swapChain->ppSwapchainRenderTargets[0], depthBuffer->mDesc.mFormat))
		{
			return false;
		}
		#endif

		ClearValue maxClearValue;
		maxClearValue.r = 1.0f;
		maxClearValue.g = 1.0f;
		maxClearValue.b = 1.0f;
		maxClearValue.a = 1.0f;

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

		VertexLayout floorVertexLayout = vertexLayout;
		GraphicsPipelineDesc floorShadowMapPipelineSettings = pipelineSettings;
		floorShadowMapPipelineSettings.pVertexLayout = &floorVertexLayout;
		floorShadowMapPipelineSettings.pBlendState = noColorWriteBlendState;

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

		// layout and pipeline for skybox draw
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

		GraphicsPipelineDesc shadowMapParticlesPipelineSettings = pipelineSettings;
		shadowMapParticlesPipelineSettings.mRenderTargetCount = SHADOW_MAP_BUFFERS_COUNT;
		shadowMapParticlesPipelineSettings.pDepthState = particlesDepthState;
		shadowMapParticlesPipelineSettings.mDepthStencilFormat = shadowMapDepthBuffer->mDesc.mFormat;
		
		ImageFormat::Enum shadowMapColorFormats[SHADOW_MAP_BUFFERS_COUNT] = { shadowMapColor->mDesc.mFormat, shadowMapDepth->mDesc.mFormat };
		shadowMapParticlesPipelineSettings.pColorFormats = shadowMapColorFormats;

		bool shadowMapSrgbValues[SHADOW_MAP_BUFFERS_COUNT] = { shadowMapColor->mDesc.mSrgb, shadowMapDepth->mDesc.mSrgb };
		shadowMapParticlesPipelineSettings.pSrgbValues = shadowMapSrgbValues;
		
		shadowMapParticlesPipelineSettings.mSampleCount = SAMPLE_COUNT_1;
		shadowMapParticlesPipelineSettings.mSampleQuality = 0;
		shadowMapParticlesPipelineSettings.pRootSignature = particlesRootSignature;
		shadowMapParticlesPipelineSettings.pRasterizerState = particlesRasterizerState;
		shadowMapParticlesPipelineSettings.pShaderProgram = particlesShadowShader;
		shadowMapParticlesPipelineSettings.pVertexLayout = &particleVertexLayout;
		shadowMapParticlesPipelineSettings.pBlendState = particlesBlendState;
		addPipeline(renderer, &shadowMapParticlesPipelineSettings, &shadowMapParticlesPassPipeline);

		floorShadowMapPipelineSettings.mRenderTargetCount = shadowMapParticlesPipelineSettings.mRenderTargetCount;
		floorShadowMapPipelineSettings.pColorFormats = shadowMapParticlesPipelineSettings.pColorFormats;
		floorShadowMapPipelineSettings.pSrgbValues = shadowMapParticlesPipelineSettings.pSrgbValues;
		floorShadowMapPipelineSettings.mSampleCount = shadowMapParticlesPipelineSettings.mSampleCount;
		floorShadowMapPipelineSettings.mSampleQuality = shadowMapParticlesPipelineSettings.mSampleQuality;
		addPipeline(renderer, &floorShadowMapPipelineSettings, &floorShadowMapDepthOnlyPassPipeline);

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
		removePipeline(renderer, floorShadowMapDepthOnlyPassPipeline);
		removePipeline(renderer, particlesPipeline);
		
		removePipeline(renderer, shadowMapParticlesPassPipeline);

		removeRenderTarget(renderer, shadowMapDepth);
		removeRenderTarget(renderer, shadowMapColor);

		removeSwapChain(renderer, swapChain);
		removeRenderTarget(renderer, depthBuffer);
		removeRenderTarget(renderer, shadowMapDepthBuffer);
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

		emitter.update(deltaTime);

		const auto particlesComponentMultiplier = sizeof(float) * emitter.getAliveParticlesCount();

		emitter.sort(cameraView);
		ASSERT(emitter.getAliveParticlesCount() <= MAX_PARTICLES_COUNT);
		::memcpy(particlesFinalRender.positions, emitter.getPositions(), particlesComponentMultiplier * CONST_BUFFER_QUANT_SIZE);
		::memcpy(particlesFinalRender.timeAndStyle, emitter.getBehaviors(), particlesComponentMultiplier * CONST_BUFFER_QUANT_SIZE);

		emitter.sort(lightView);
		ASSERT(emitter.getAliveParticlesCount() <= MAX_PARTICLES_COUNT);
		::memcpy(particlesShadowMap.positions, emitter.getPositions(), particlesComponentMultiplier * CONST_BUFFER_QUANT_SIZE);
		::memcpy(particlesShadowMap.timeAndStyle, emitter.getBehaviors(), particlesComponentMultiplier * CONST_BUFFER_QUANT_SIZE);

		shadowMapUniforms.mCamera = lightView;
		shadowMapUniforms.mProjectView = lightProjection * lightView;
		shadowMapUniforms.zProjection.setX(SHADOW_MAP_Z_FAR / (SHADOW_MAP_Z_FAR - SHADOW_MAP_Z_NEAR));
		shadowMapUniforms.zProjection.setY(-SHADOW_MAP_Z_FAR * SHADOW_MAP_Z_NEAR / (SHADOW_MAP_Z_FAR - SHADOW_MAP_Z_NEAR));
		shadowMapUniforms.zProjection.setZ(0);
		shadowMapUniforms.zProjection.setW(0);

		shadowReceiversUniforms.shadowMapCamera = lightView;
		shadowReceiversUniforms.shadowMapMvp = lightProjection * lightView;

		cameraView.setTranslation(vec3(0));
		skyBoxUniformData = commonUniformData;
		skyBoxUniformData.mProjectView = projection * cameraView;
		skyBoxUniformData.mCamera = cameraView;
		skyBoxUniformData.zProjection = commonUniformData.zProjection;
	}

private:

	void drawShadowMap(Cmd* const cmd)
	{
		ASSERT(nullptr != cmd);

		const int MAX_DESCRIPTORS_COUNT = 8;

		cmdBeginDebugMarker(cmd, 0.5f, 0.5f, 0, "draw particles shadow map");
		{
			// render target barrier
			{
			TextureBarrier shadowMapDepthPassBarrier[] =
			{
				{ shadowMapDepth->pTexture, RESOURCE_STATE_RENDER_TARGET },
				{ shadowMapColor->pTexture, RESOURCE_STATE_RENDER_TARGET },
				{ shadowMapDepthBuffer->pTexture, RESOURCE_STATE_DEPTH_WRITE },
			};
			cmdResourceBarrier(cmd, 0, nullptr, _countof(shadowMapDepthPassBarrier), shadowMapDepthPassBarrier, false);
			}

			// render target setup
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
			shadowMapLoadActions.mLoadActionDepth = LOAD_ACTION_CLEAR;
			shadowMapLoadActions.mClearDepth.depth = 1.0f;
			shadowMapLoadActions.mClearDepth.stencil = 0;

			RenderTarget* renderTargets[SHADOW_MAP_BUFFERS_COUNT] = { shadowMapColor, shadowMapDepth, };
			cmdBindRenderTargets(cmd, _countof(renderTargets), renderTargets, shadowMapDepthBuffer, &shadowMapLoadActions, nullptr, nullptr, -1, -1);
			cmdSetViewport(cmd, 0.0f, 0.0f, static_cast<float>(shadowMapDepth->mDesc.mWidth), static_cast<float>(shadowMapDepth->mDesc.mHeight), 0.0f, 1.0f);
			cmdSetScissor(cmd, 0, 0, shadowMapDepth->mDesc.mWidth, shadowMapDepth->mDesc.mHeight);
			}

			// floor z-only
			{
			DescriptorData floorDrawParameters[MAX_DESCRIPTORS_COUNT] = {};

			auto descriptorCount = 0u;

			floorDrawParameters[descriptorCount].pName = "ProjectionUniforms";
			floorDrawParameters[descriptorCount++].ppBuffers = &shadowMapUniformsBuffers[preRenderedFrameIndex];

			ASSERT(descriptorCount <= _countof(floorDrawParameters));

			cmdBeginDebugMarker(cmd, 1, 1, 1, "Draw floor");

			cmdBindPipeline(cmd, floorShadowMapDepthOnlyPassPipeline);

			cmdBindDescriptors(cmd, floorRootSignature, descriptorCount, floorDrawParameters);
			cmdBindVertexBuffer(cmd, /*buffer count: */1, &floorVertices,  /*offsets: */nullptr);

			cmdDraw(cmd, FLOOR_VERTEX_COUNT, /*first vertex: */0);

			cmdEndDebugMarker(cmd);
			}

			// particles shadow map
			{
			TextureBarrier zReadBarrier[] =
			{
				{ shadowMapDepthBuffer->pTexture, RESOURCE_STATE_DEPTH_READ | RESOURCE_STATE_SHADER_RESOURCE },
			};
			cmdResourceBarrier(cmd, 0, nullptr, _countof(zReadBarrier), zReadBarrier, false);

			DescriptorData particlesShadowMapDescriptors[MAX_DESCRIPTORS_COUNT] = {};
			int descriptorCount = 0;
			particlesShadowMapDescriptors[descriptorCount].pName = "ProjectionUniforms";
			particlesShadowMapDescriptors[descriptorCount++].ppBuffers = &shadowMapUniformsBuffers[preRenderedFrameIndex];
			particlesShadowMapDescriptors[descriptorCount].pName = "particlesInstances";
			particlesShadowMapDescriptors[descriptorCount++].ppBuffers = &particlesPerInstanceDataShadowMap[preRenderedFrameIndex];
			particlesShadowMapDescriptors[descriptorCount].pName = "image";
			particlesShadowMapDescriptors[descriptorCount++].ppTextures = &particlesTexture;
			particlesShadowMapDescriptors[descriptorCount].pName = "depthBuffer";
			particlesShadowMapDescriptors[descriptorCount++].ppTextures = &shadowMapDepthBuffer->pTexture;

			cmdBindPipeline(cmd, shadowMapParticlesPassPipeline);			
			cmdBindDescriptors(cmd, particlesRootSignature, descriptorCount, particlesShadowMapDescriptors);
			cmdBindVertexBuffer(cmd, 1, &particleVertices, nullptr);
			cmdDrawInstanced(cmd, particleVertexCount, 0, emitter.getAliveParticlesCount(), 0);
			}

			cmdBindRenderTargets(cmd, 0, nullptr, nullptr, nullptr, nullptr, nullptr, -1, -1);
		}
		cmdEndDebugMarker(cmd);
	}

	void drawScene(RenderTarget* frameBufferRenderTarget, Cmd* const cmd)
	{
		ASSERT(nullptr != frameBufferRenderTarget);
		ASSERT(nullptr != cmd);

		const int MAX_DESCRIPTORS_COUNT = 8;

		// render target barrier
		{
		TextureBarrier barriers[] = 
		{
			{ frameBufferRenderTarget->pTexture, RESOURCE_STATE_RENDER_TARGET },
			{ depthBuffer->pTexture, RESOURCE_STATE_DEPTH_WRITE },
		};
		cmdResourceBarrier(cmd, 0, nullptr, _countof(barriers), barriers, false);
		}

		// render target setup
		{
		LoadActionsDesc loadActions = {};
		loadActions.mLoadActionsColor[0] = LOAD_ACTION_CLEAR;
		loadActions.mClearColorValues[0].r = 1.0f;
		loadActions.mClearColorValues[0].g = 1.0f;
		loadActions.mClearColorValues[0].b = 0.0f;
		loadActions.mClearColorValues[0].a = 0.0f;
		loadActions.mLoadActionDepth = LOAD_ACTION_CLEAR;
		loadActions.mClearDepth.depth = 1.0f;
		loadActions.mClearDepth.stencil = 0;

		cmdBindRenderTargets(cmd, 1, &frameBufferRenderTarget, depthBuffer, &loadActions, nullptr, nullptr, -1, -1);
		cmdSetViewport(cmd, 0.0f, 0.0f, static_cast<float>(frameBufferRenderTarget->mDesc.mWidth), static_cast<float>(frameBufferRenderTarget->mDesc.mHeight), 0.0f, 1.0f);
		cmdSetScissor(cmd, 0, 0, frameBufferRenderTarget->mDesc.mWidth, frameBufferRenderTarget->mDesc.mHeight);
		}

		cmdBeginDebugMarker(cmd, 0, 0, 1, "draw sky box");
		{
			cmdBindPipeline(cmd, skyBoxPipeline);

			DescriptorData params[MAX_DESCRIPTORS_COUNT] = {};
			auto descriptorCount = 0u;

			params[descriptorCount].pName = "ProjectionUniforms";
			params[descriptorCount++].ppBuffers = &skyBoxUniformBuffers[preRenderedFrameIndex];
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
			cmdBindVertexBuffer(cmd, 1, &skyBoxVertexBuffer, nullptr);
			cmdDraw(cmd, 36, 0);
		}
		cmdEndDebugMarker(cmd);

		cmdBeginDebugMarker(cmd, 1, 1, 1, "draw floor");
		{
			DescriptorData floorDrawParameters[MAX_DESCRIPTORS_COUNT] = {};
		
			auto descriptorCount = 0u;

			floorDrawParameters[descriptorCount].pName = "ProjectionUniforms";
			floorDrawParameters[descriptorCount++].ppBuffers = &cameraViewProjectionUniformBuffers[preRenderedFrameIndex];
		
			floorDrawParameters[descriptorCount].pName = "shadowReceiverUniforms";
			floorDrawParameters[descriptorCount++].ppBuffers = &shadowReceiversUniformBuffer[preRenderedFrameIndex];

			floorDrawParameters[descriptorCount].pName = "shadowMapDepth";
			floorDrawParameters[descriptorCount++].ppTextures = &shadowMapDepth->pTexture;

			floorDrawParameters[descriptorCount].pName = "shadowMapColor";
			floorDrawParameters[descriptorCount++].ppTextures = &shadowMapColor->pTexture;

			ASSERT(descriptorCount <= _countof(floorDrawParameters));

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

			
		}
		cmdEndDebugMarker(cmd);

		cmdBeginDebugMarker(cmd, 0.5f, 0.5f, 1, "draw particles");
		{
			DescriptorData particlesDescriptors[MAX_DESCRIPTORS_COUNT] = {};
			auto descriptorCount = 0u;

			particlesDescriptors[descriptorCount].pName = "ProjectionUniforms";
			particlesDescriptors[descriptorCount++].ppBuffers = &cameraViewProjectionUniformBuffers[preRenderedFrameIndex];

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
				{ frameBufferRenderTarget->pTexture, RESOURCE_STATE_RENDER_TARGET },
				{ depthBuffer->pTexture, RESOURCE_STATE_DEPTH_READ | RESOURCE_STATE_SHADER_RESOURCE },
			};
			cmdResourceBarrier(cmd, 0, nullptr, _countof(zReadBarrier), zReadBarrier, /*batch: */false);
			cmdBindDescriptors(cmd, particlesRootSignature, descriptorCount, particlesDescriptors);
			cmdBindVertexBuffer(cmd, 1, &particleVertices,  /*offsets: */nullptr);
			cmdDrawInstanced(cmd, particleVertexCount,  /*first vertex: */0, emitter.getAliveParticlesCount(),  /*first instance: */0);
		}
		cmdEndDebugMarker(cmd);
	}

	bool addDepthBuffers()
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

		depthRenderTargetDescription.mWidth = SHADOW_MAP_WIDTH;
		depthRenderTargetDescription.mHeight = SHADOW_MAP_HEIGHT;
		addRenderTarget(renderer, &depthRenderTargetDescription, &shadowMapDepthBuffer);

		return nullptr != depthBuffer && nullptr != shadowMapDepthBuffer;
	}

	void recenterCameraView(const float maxDistance) const
	{
		const vec3 lookAt = vec3{ 0 };

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

public:

	void Draw() override
	{
		acquireNextImage(renderer, swapChain, imageAcquiredSemaphore, /*p_fence: */nullptr, &preRenderedFrameIndex);
		ASSERT(0 <= preRenderedFrameIndex && PRE_RENDERED_FRAMES_COUNT > preRenderedFrameIndex);

		RenderTarget* frameBufferRenderTarget = swapChain->ppSwapchainRenderTargets[preRenderedFrameIndex];
		Semaphore* frameCompleteSemaphore = frameCompleteSemaphores[preRenderedFrameIndex];
		Fence* frameCompleteFence = frameCompleteFences[preRenderedFrameIndex];

		// Stall if CPU is running "Swap Chain Buffer Count" frames ahead of GPU
		FenceStatus fenceStatus;
		getFenceStatus(renderer, frameCompleteFence, &fenceStatus);
		if (fenceStatus == FENCE_STATUS_INCOMPLETE)
		{
			waitForFences(commandQueue, 1, &frameCompleteFence, false);
		}
			
		BufferUpdateDesc cameraViewProjectionUniformsUpdate = { cameraViewProjectionUniformBuffers[preRenderedFrameIndex], &commonUniformData };
		updateResource(&cameraViewProjectionUniformsUpdate);

		BufferUpdateDesc skyBoxViewProjectionUpdate = { skyBoxUniformBuffers[preRenderedFrameIndex], &skyBoxUniformData };
		updateResource(&skyBoxViewProjectionUpdate);

		BufferUpdateDesc particlesUpdateDescription = { particlesPerInstanceData[preRenderedFrameIndex], &particlesFinalRender };
		updateResource(&particlesUpdateDescription);

		BufferUpdateDesc particlesShadowMapUpdateDescription = { particlesPerInstanceDataShadowMap[preRenderedFrameIndex], &particlesShadowMap };
		updateResource(&particlesShadowMapUpdateDescription);

		BufferUpdateDesc particlesShadowUniformsDescription = { shadowMapUniformsBuffers[preRenderedFrameIndex], &shadowMapUniforms };
		updateResource(&particlesShadowUniformsDescription);

		BufferUpdateDesc shadowReceiversUniformsDescription = { shadowReceiversUniformBuffer[preRenderedFrameIndex], &shadowReceiversUniforms };
		updateResource(&shadowReceiversUniformsDescription);

		Cmd* cmd = commandsBuffers[preRenderedFrameIndex];
		beginCmd(cmd);

		drawShadowMap(cmd);
		drawScene(frameBufferRenderTarget, cmd);

		cmdBeginDebugMarker(cmd, 0, 1, 0, "Draw UI");
		static HiresTimer gTimer;
		gTimer.GetUSec(true);

		#if defined(NEED_JOYSTICK)
		gVirtualJoystick.Draw(cmd, pCameraController, { 1.0f, 1.0f, 1.0f, 1.0f });
		#endif

		drawDebugText(cmd, /*x: */8, /*y: */15, tinystl::string::format("CPU %f ms", gTimer.GetUSecAverage() / 1000.0f), &frameTimeDraw);
		userInterface.Draw(cmd);
		cmdBindRenderTargets(cmd, 0, nullptr, nullptr, nullptr, nullptr, nullptr, -1, -1);
		cmdEndDebugMarker(cmd);

		{
		TextureBarrier barriers[] = { { frameBufferRenderTarget->pTexture, RESOURCE_STATE_PRESENT } };
		cmdResourceBarrier(cmd, 0, nullptr, _countof(barriers), barriers, true);
		}
		endCmd(cmd);

		queueSubmit(commandQueue, 1, &cmd, frameCompleteFence, 1, &imageAcquiredSemaphore, 1, &frameCompleteSemaphore);
		queuePresent(commandQueue, swapChain, preRenderedFrameIndex, 1, &frameCompleteSemaphore);
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

};

DEFINE_APPLICATION_MAIN(Particles)