/*
 * Copyright (c) 2018 Confetti Interactive Inc.
 *
 * This file is part of The-Forge
 * (see https://github.com/ConfettiFX/The-Forge).
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
*/

#include "01_Emitter.h"

#include "../../../../Common_3/ThirdParty/OpenSource/TinySTL/vector.h"
#include "../../../../Common_3/ThirdParty/OpenSource/TinySTL/string.h"

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

struct Particles final
{
	float positions[CONST_BUFFER_QUANT_SIZE * MAX_PARTICLES_COUNT];
	float timeAndStyle[CONST_BUFFER_QUANT_SIZE * MAX_PARTICLES_COUNT];

	vec4 colorAndSizeScale[PARTICLES_STYLES_COUNT];
	
	float particlesLifeLength;
};

const uint32_t	  gImageCount = 3;

Renderer*		   pRenderer = NULL;

Queue*			  pGraphicsQueue = NULL;
CmdPool*			pCmdPool = NULL;
Cmd**			   ppCmds = NULL;

SwapChain*		  pSwapChain = NULL;
RenderTarget*	   pDepthBuffer = NULL;
Fence*			  pRenderCompleteFences[gImageCount] = { NULL };
Semaphore*		  pImageAcquiredSemaphore = NULL;
Semaphore*		  pRenderCompleteSemaphores[gImageCount] = { NULL };

Shader*			 pSkyBoxDrawShader = NULL;
Buffer*			 pSkyBoxVertexBuffer = NULL;
Pipeline*		   pSkyBoxDrawPipeline = NULL;
RootSignature*	  pRootSignature = NULL;
Sampler*			pSamplerSkyBox = NULL;
Texture*			pSkyBoxTextures[6];
#if defined(NEED_JOYSTICK)
VirtualJoystickUI   gVirtualJoystick;
#endif
DepthState*		 pDepth = NULL;
RasterizerState*	pSkyboxRast = NULL;
RasterizerState*	pSphereRast = NULL;

Buffer*				pProjViewUniformBuffer[gImageCount] = { NULL };
Buffer*				pSkyboxUniformBuffer[gImageCount] = { NULL };

uint32_t			gFrameIndex = 0;

UniformBlock		gUniformData;
UniformBlock		gUniformDataSky;

ICameraController*  pCameraController = NULL;

UIApp			   gAppUI;

FileSystem		  gFileSystem;
LogManager        gLogManager;

const char*		 pSkyBoxImageFileNames[] =
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
	"../../../src/01_Transformations/", // FSR_BinShaders
	"../../../src/01_Transformations/", // FSR_SrcShaders
	"../../../UnitTestResources/",		// FSR_BinShaders_Common
	"../../../UnitTestResources/",		// FSR_SrcShaders_Common
	"../../../UnitTestResources/",		// FSR_Textures
	"../../../UnitTestResources/",		// FSR_Meshes
	"../../../UnitTestResources/",		// FSR_Builtin_Fonts
	"../../../src/01_Transformations/",	// FSR_GpuConfig
	"",									// FSR_OtherFiles
};

TextDrawDesc gFrameTimeDraw = TextDrawDesc(0, 0xff000000, 18);

class Transformations final : public IApp
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
	Buffer* shadowReceiversUniformBuffer[gImageCount] = { nullptr };

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
	Buffer* shadowMapUniformsBuffers[gImageCount] = { nullptr };
	Emitter emitter;

	Particles particlesFinalRender;
	Particles particlesShadowMap;

	Buffer* particlesPerInstanceData[gImageCount] = { nullptr };
	Buffer* particlesPerInstanceDataShadowMap[gImageCount] = { nullptr };

	void prepareFloorResources()
	{
		ShaderLoadDesc floorShaderSource = {};
		floorShaderSource.mStages[0] = { "floor.vert", nullptr, 0, FSR_SrcShaders };
		floorShaderSource.mStages[1] = { "floor.frag", nullptr, 0, FSR_SrcShaders };
		addShader(pRenderer, &floorShaderSource, &floorShader);

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

		RasterizerStateDesc rasterizerStateDescription = {};
		rasterizerStateDescription.mCullMode = CULL_MODE_FRONT;
		addRasterizerState(pRenderer, &rasterizerStateDescription, &floorRasterizerState);

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
		addSampler(pRenderer, &shadowMapSamplerDescription, &shadowMapSampler);

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
			addRootSignature(pRenderer, &rootDescription, &floorRootSignature);
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
		addShader(pRenderer, &particlesShaderSource, &particlesShader);

		ShaderLoadDesc particlesShadowDepthColorShaderSource = {};
		particlesShadowDepthColorShaderSource.mStages[0] = { "particle_shadow_map.vert", nullptr, 0, FSR_SrcShaders };
		particlesShadowDepthColorShaderSource.mStages[1] = { "particle_shadow_map.frag", nullptr, 0, FSR_SrcShaders };
		addShader(pRenderer, &particlesShadowDepthColorShaderSource, &particlesShadowShader);

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
		addDepthState(pRenderer, &depthStateDescription, &particlesDepthState);

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
		
		addBlendState(pRenderer, &blendStateDescription, &particlesBlendState);

		TextureLoadDesc textureDescription = {};
		textureDescription.mRoot = FSR_Textures;
		textureDescription.mUseMipmaps = true;
		textureDescription.pFilename = "blackSmoke00.png";
		textureDescription.ppTexture = &particlesTexture;
		addResource(&textureDescription, true);

		RasterizerStateDesc rasterizerStateDescription = {};
		rasterizerStateDescription.mCullMode = CULL_MODE_NONE;
		addRasterizerState(pRenderer, &rasterizerStateDescription, &particlesRasterizerState);

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
		addSampler(pRenderer, &samplerDescription, &particleImageSampler);

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
			addRootSignature(pRenderer, &rootDescription, &particlesRootSignature);
		}

		BufferLoadDesc perInstanceBufferDescription = {};
		perInstanceBufferDescription.mDesc.mDescriptors = DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		perInstanceBufferDescription.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
		perInstanceBufferDescription.mDesc.mSize = sizeof(Particles);
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
		addDepthState(pRenderer, &disableAllDepthStateDescription, &depthStencilStateDisableAll);

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
	
	Transformations()
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
		initRenderer(GetName(), &settings, &pRenderer);
		
		if (!pRenderer)
		{
			return false;
		}
			
		QueueDesc queueDesc = {};
		queueDesc.mType = CMD_POOL_DIRECT;
		addQueue(pRenderer, &queueDesc, &pGraphicsQueue);
		addCmdPool(pRenderer, pGraphicsQueue, false, &pCmdPool);
		addCmd_n(pCmdPool, false, gImageCount, &ppCmds);

		for (uint32_t i = 0; i < gImageCount; ++i)
		{
			addFence(pRenderer, &pRenderCompleteFences[i]);
			addSemaphore(pRenderer, &pRenderCompleteSemaphores[i]);
		}
		addSemaphore(pRenderer, &pImageAcquiredSemaphore);

		initResourceLoaderInterface(pRenderer, DEFAULT_MEMORY_BUDGET, true);
		initDebugRendererInterface(pRenderer, "TitilliumText/TitilliumText-Bold.otf", FSR_Builtin_Fonts);

		for (int i = 0; i < 6; ++i)
		{
			TextureLoadDesc textureDesc = {};
			textureDesc.mRoot = FSR_Textures;
			textureDesc.mUseMipmaps = true;
			textureDesc.pFilename = pSkyBoxImageFileNames[i];
			textureDesc.ppTexture = &pSkyBoxTextures[i];
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
		
		addShader(pRenderer, &skyShader, &pSkyBoxDrawShader);

		SamplerDesc samplerDesc = 
		{
			FILTER_LINEAR, FILTER_LINEAR, MIPMAP_MODE_NEAREST,
			ADDRESS_MODE_CLAMP_TO_EDGE, ADDRESS_MODE_CLAMP_TO_EDGE, ADDRESS_MODE_CLAMP_TO_EDGE
		};
		addSampler(pRenderer, &samplerDesc, &pSamplerSkyBox);

		Shader* shaders[] = { pSkyBoxDrawShader };
		const char* pStaticSamplersNames[] = { "uSampler0" };
		Sampler* staticSamplers[] = { pSamplerSkyBox };
		RootSignatureDesc rootDesc = {};
		rootDesc.mStaticSamplerCount = _countof(pStaticSamplersNames);
		rootDesc.ppStaticSamplerNames = pStaticSamplersNames;
		rootDesc.ppStaticSamplers = staticSamplers;
		rootDesc.mShaderCount = _countof(shaders);
		rootDesc.ppShaders = shaders;
		addRootSignature(pRenderer, &rootDesc, &pRootSignature);

		RasterizerStateDesc rasterizerStateDesc = {};
		rasterizerStateDesc.mCullMode = CULL_MODE_NONE;
		addRasterizerState(pRenderer, &rasterizerStateDesc, &pSkyboxRast);

		RasterizerStateDesc sphereRasterizerStateDesc = {};
		sphereRasterizerStateDesc.mCullMode = CULL_MODE_FRONT;
		addRasterizerState(pRenderer, &sphereRasterizerStateDesc, &pSphereRast);

		DepthStateDesc depthStateDesc = {};
		depthStateDesc.mDepthTest = true;
		depthStateDesc.mDepthWrite = true;
		depthStateDesc.mDepthFunc = CMP_LEQUAL;
		addDepthState(pRenderer, &depthStateDesc, &pDepth);

		//Generate sky box vertex buffer
		float skyBoxPoints[] = 
		{
			10.0f,  -10.0f, -10.0f,6.0f, // -z
			-10.0f, -10.0f, -10.0f,6.0f,
			-10.0f, 10.0f, -10.0f,6.0f,
			-10.0f, 10.0f, -10.0f,6.0f,
			10.0f,  10.0f, -10.0f,6.0f,
			10.0f,  -10.0f, -10.0f,6.0f,

			-10.0f, -10.0f,  10.0f,2.0f,  //-x
			-10.0f, -10.0f, -10.0f,2.0f,
			-10.0f,  10.0f, -10.0f,2.0f,
			-10.0f,  10.0f, -10.0f,2.0f,
			-10.0f,  10.0f,  10.0f,2.0f,
			-10.0f, -10.0f,  10.0f,2.0f,

			10.0f, -10.0f, -10.0f,1.0f, //+x
			10.0f, -10.0f,  10.0f,1.0f,
			10.0f,  10.0f,  10.0f,1.0f,
			10.0f,  10.0f,  10.0f,1.0f,
			10.0f,  10.0f, -10.0f,1.0f,
			10.0f, -10.0f, -10.0f,1.0f,

			-10.0f, -10.0f,  10.0f,5.0f,  // +z
			-10.0f,  10.0f,  10.0f,5.0f,
			10.0f,  10.0f,  10.0f,5.0f,
			10.0f,  10.0f,  10.0f,5.0f,
			10.0f, -10.0f,  10.0f,5.0f,
			-10.0f, -10.0f,  10.0f,5.0f,

			-10.0f,  10.0f, -10.0f, 3.0f,  //+y
			10.0f,  10.0f, -10.0f,3.0f,
			10.0f,  10.0f,  10.0f,3.0f,
			10.0f,  10.0f,  10.0f,3.0f,
			-10.0f,  10.0f,  10.0f,3.0f,
			-10.0f,  10.0f, -10.0f,3.0f,

			10.0f,  -10.0f, 10.0f, 4.0f,  //-y
			10.0f,  -10.0f, -10.0f,4.0f,
			-10.0f,  -10.0f,  -10.0f,4.0f,
			-10.0f,  -10.0f,  -10.0f,4.0f,
			-10.0f,  -10.0f,  10.0f,4.0f,
			10.0f,  -10.0f, 10.0f,4.0f,
		};

		uint64_t skyBoxDataSize = 4 * 6 * 6 * sizeof(float);
		BufferLoadDesc skyboxVbDesc = {};
		skyboxVbDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_VERTEX_BUFFER;
		skyboxVbDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_GPU_ONLY;
		skyboxVbDesc.mDesc.mSize = skyBoxDataSize;
		skyboxVbDesc.mDesc.mVertexStride = sizeof(float) * 4;
		skyboxVbDesc.pData = skyBoxPoints;
		skyboxVbDesc.ppBuffer = &pSkyBoxVertexBuffer;
		addResource(&skyboxVbDesc);

		BufferLoadDesc ubDesc = {};
		ubDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		ubDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
		ubDesc.mDesc.mSize = sizeof(UniformBlock);
		ubDesc.mDesc.mFlags = BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
		ubDesc.pData = nullptr;
		for (uint32_t i = 0; i < gImageCount; ++i)
		{
			ubDesc.ppBuffer = &pProjViewUniformBuffer[i];
			addResource(&ubDesc);
			ubDesc.ppBuffer = &pSkyboxUniformBuffer[i];
			addResource(&ubDesc);
		}

		finishResourceLoading();

		if (!gAppUI.Init(pRenderer))
		{
			return false;
		}

		gAppUI.LoadFont("TitilliumText/TitilliumText-Bold.otf", FSR_Builtin_Fonts);

		const CameraMotionParameters cmp { 160.0f, 600.0f, 200.0f };
		const vec3 camPos { 48.0f, 48.0f, 20.0f };
		const vec3 lookAt { 0 };

		pCameraController = createFpsCameraController(camPos, lookAt);
		requestMouseCapture(true);

		pCameraController->setMotionParameters(cmp);

		InputSystem::RegisterInputEvent(cameraInputEvent);

		return true;
	}

	void Exit() override
	{
		waitForFences(pGraphicsQueue, 1, &pRenderCompleteFences[gFrameIndex], true);

		destroyCameraController(pCameraController);

		removeDebugRendererInterface();

		#if defined(NEED_JOYSTICK)
		gVirtualJoystick.Exit();
		#endif

		gAppUI.Exit();

		ASSERT(gImageCount == _countof(pProjViewUniformBuffer));
		ASSERT(gImageCount == _countof(pSkyboxUniformBuffer));
		for (uint32_t i = 0; i < gImageCount; ++i)
		{
			removeResource(pProjViewUniformBuffer[i]);
			removeResource(pSkyboxUniformBuffer[i]);
		}

		removeResource(pSkyBoxVertexBuffer);

		for (auto& texture : pSkyBoxTextures)
		{
			removeResource(texture);
		}
			

		{
			removeShader(pRenderer, floorShader);
			removeResource(floorVertices);
			removeRasterizerState(floorRasterizerState);
			
			removeSampler(pRenderer, shadowMapSampler);
			removeRootSignature(pRenderer, floorRootSignature);

			removeResource(particleVertices);
			removeRasterizerState(particlesRasterizerState);

			removeDepthState(particlesDepthState);
			removeBlendState(particlesBlendState);

			removeSampler(pRenderer, particleImageSampler);
			removeResource(particlesTexture);
			removeShader(pRenderer, particlesShader);
			removeRootSignature(pRenderer, particlesRootSignature);

			removeShader(pRenderer, particlesShadowShader);

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

		removeSampler(pRenderer, pSamplerSkyBox);
		removeShader(pRenderer, pSkyBoxDrawShader);
		removeRootSignature(pRenderer, pRootSignature);

		removeDepthState(pDepth);
		removeRasterizerState(pSphereRast);
		removeRasterizerState(pSkyboxRast);

		for (uint32_t i = 0; i < gImageCount; ++i)
		{
			removeFence(pRenderer, pRenderCompleteFences[i]);
			removeSemaphore(pRenderer, pRenderCompleteSemaphores[i]);
		}
		removeSemaphore(pRenderer, pImageAcquiredSemaphore);

		removeCmd_n(pCmdPool, gImageCount, ppCmds);
		removeCmdPool(pRenderer, pCmdPool);

		removeResourceLoaderInterface(pRenderer);
		removeQueue(pGraphicsQueue);
		removeRenderer(pRenderer);
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
			
		if (!gAppUI.Load(pSwapChain->ppSwapchainRenderTargets))
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

		addRenderTarget(pRenderer, &shadowMapDepthBufferDescription, &shadowMapDepth);

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

		addRenderTarget(pRenderer, &shadowMapColorBufferDescription, &shadowMapColor);

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
		pipelineSettings.pDepthState = pDepth;
		pipelineSettings.pColorFormats = &pSwapChain->ppSwapchainRenderTargets[0]->mDesc.mFormat;
		pipelineSettings.pSrgbValues = &pSwapChain->ppSwapchainRenderTargets[0]->mDesc.mSrgb;
		pipelineSettings.mSampleCount = pSwapChain->ppSwapchainRenderTargets[0]->mDesc.mSampleCount;
		pipelineSettings.mSampleQuality = pSwapChain->ppSwapchainRenderTargets[0]->mDesc.mSampleQuality;
		pipelineSettings.mDepthStencilFormat = pDepthBuffer->mDesc.mFormat;
		pipelineSettings.pVertexLayout = &vertexLayout;
		pipelineSettings.pRasterizerState = pSphereRast;

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
		addPipeline(pRenderer, &pipelineSettings, &floorPipeline);

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
		addPipeline(pRenderer, &pipelineSettings, &particlesPipeline);

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
		pipelineSettings.pRasterizerState = pSkyboxRast;
		pipelineSettings.pShaderProgram = pSkyBoxDrawShader;
		pipelineSettings.pRootSignature = pRootSignature;
		pipelineSettings.pVertexLayout = &vertexLayout;
		addPipeline(pRenderer, &pipelineSettings, &pSkyBoxDrawPipeline);

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
		addPipeline(pRenderer, &shadowMapPipelineSettings, &shadowMapPassPipeline);

		return true;
	}

	void Unload() override
	{
		waitForFences(pGraphicsQueue, gImageCount, pRenderCompleteFences, /*signel: */true);

		gAppUI.Unload();

		#if defined(NEED_JOYSTICK)
		gVirtualJoystick.Unload();
		#endif

		removePipeline(pRenderer, pSkyBoxDrawPipeline);
		
		removePipeline(pRenderer, floorPipeline);
		removePipeline(pRenderer, particlesPipeline);
		
		removePipeline(pRenderer, shadowMapPassPipeline);

		removeRenderTarget(pRenderer, shadowMapDepth);
		removeRenderTarget(pRenderer, shadowMapColor);

		removeSwapChain(pRenderer, pSwapChain);
		removeRenderTarget(pRenderer, pDepthBuffer);
	}

	void Update(const float deltaTime) override
	{
		if (getKeyDown(KEY_BUTTON_X))
		{
			recenterCameraView(170.0f);
		}

		pCameraController->update(deltaTime);
		
		mat4 cameraView = pCameraController->getViewMatrix();

		const float aspectInverse = static_cast<float>(mSettings.mHeight) / static_cast<float>(mSettings.mWidth);
		const float horizontal_fov = PI / 2.0f;
		const float zNear = 0.1f;
		const float zFar = 1000.0f;

		const mat4 projection = mat4::perspective(horizontal_fov, aspectInverse, zNear, zFar);
		gUniformData.mProjectView = projection * cameraView;
		gUniformData.mCamera = cameraView;
		gUniformData.zProjection.setX(zFar / (zFar - zNear));
		gUniformData.zProjection.setY(- zFar * zNear / (zFar - zNear));
		gUniformData.zProjection.setZ(0);
		gUniformData.zProjection.setW(0);
		gUniformData.mLightPosition = vec3(0, 0, 0);
		gUniformData.mLightColor = vec3(0.9f, 0.9f, 0.7f); // Pale Yellow

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
		gUniformDataSky = gUniformData;
		gUniformDataSky.mProjectView = projection * cameraView;
		gUniformDataSky.mCamera = cameraView;
		gUniformDataSky.zProjection.setX(zFar / (zFar - zNear));
		gUniformDataSky.zProjection.setY(-zFar * zNear / (zFar - zNear));
		gUniformDataSky.zProjection.setZ(0);
		gUniformDataSky.zProjection.setW(0);
	}

	void Draw() override
	{
		acquireNextImage(pRenderer, pSwapChain, pImageAcquiredSemaphore, NULL, &gFrameIndex);

		RenderTarget* pRenderTarget = pSwapChain->ppSwapchainRenderTargets[gFrameIndex];
		Semaphore* pRenderCompleteSemaphore = pRenderCompleteSemaphores[gFrameIndex];
		Fence* pRenderCompleteFence = pRenderCompleteFences[gFrameIndex];

		// Stall if CPU is running "Swap Chain Buffer Count" frames ahead of GPU
		FenceStatus fenceStatus;
		getFenceStatus(pRenderer, pRenderCompleteFence, &fenceStatus);
		if (fenceStatus == FENCE_STATUS_INCOMPLETE)
		{
			waitForFences(pGraphicsQueue, 1, &pRenderCompleteFence, false);
		}
			
		BufferUpdateDesc viewProjCbv = { pProjViewUniformBuffer[gFrameIndex], &gUniformData };
		updateResource(&viewProjCbv);

		BufferUpdateDesc skyboxViewProjCbv = { pSkyboxUniformBuffer[gFrameIndex], &gUniformDataSky };
		updateResource(&skyboxViewProjCbv);

		BufferUpdateDesc particlesUpdateDescription = { particlesPerInstanceData[gFrameIndex], &particlesFinalRender };
		updateResource(&particlesUpdateDescription);

		BufferUpdateDesc particlesShadowMapUpdateDescription = { particlesPerInstanceDataShadowMap[gFrameIndex], &particlesShadowMap };
		updateResource(&particlesShadowMapUpdateDescription);

		BufferUpdateDesc particlesShadowUniformsDescription = { shadowMapUniformsBuffers[gFrameIndex], &shadowMapUniforms };
		updateResource(&particlesShadowUniformsDescription);

		BufferUpdateDesc shadowReceiversUniformsDescription = { shadowReceiversUniformBuffer[gFrameIndex], &shadowReceiversUniforms };
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
		
		Cmd* cmd = ppCmds[gFrameIndex];
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
			descriptors[descriptorCount++].ppBuffers = &shadowReceiversUniformBuffer[gFrameIndex];
			descriptors[descriptorCount].pName = "particlesInstances";
			descriptors[descriptorCount++].ppBuffers = &particlesPerInstanceDataShadowMap[gFrameIndex];
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
			{ pDepthBuffer->pTexture, RESOURCE_STATE_DEPTH_WRITE },
		};
		cmdResourceBarrier(cmd, 0, nullptr, _countof(barriers), barriers, false);
		}

		cmdBindRenderTargets(cmd, 1, &pRenderTarget, pDepthBuffer, &loadActions, nullptr, nullptr, -1, -1);
		cmdSetViewport(cmd, 0.0f, 0.0f, static_cast<float>(pRenderTarget->mDesc.mWidth), static_cast<float>(pRenderTarget->mDesc.mHeight), 0.0f, 1.0f);
		cmdSetScissor(cmd, 0, 0, pRenderTarget->mDesc.mWidth, pRenderTarget->mDesc.mHeight);

		cmdBeginDebugMarker(cmd, 0, 0, 1, "Draw skybox");
		{
			cmdBindPipeline(cmd, pSkyBoxDrawPipeline);

			DescriptorData params[MAX_DESCRIPTORS_COUNT] = {};
			auto descriptorCount = 0u;

			params[descriptorCount].pName = "uniformBlock";
			params[descriptorCount++].ppBuffers = &pSkyboxUniformBuffer[gFrameIndex];
			params[descriptorCount].pName = "RightText";
			params[descriptorCount++].ppTextures = &pSkyBoxTextures[0];
			params[descriptorCount].pName = "LeftText";
			params[descriptorCount++].ppTextures = &pSkyBoxTextures[1];
			params[descriptorCount].pName = "TopText";
			params[descriptorCount++].ppTextures = &pSkyBoxTextures[2];
			params[descriptorCount].pName = "BotText";
			params[descriptorCount++].ppTextures = &pSkyBoxTextures[3];
			params[descriptorCount].pName = "FrontText";
			params[descriptorCount++].ppTextures = &pSkyBoxTextures[4];
			params[descriptorCount].pName = "BackText";
			params[descriptorCount++].ppTextures = &pSkyBoxTextures[5];

			ASSERT(descriptorCount <= _countof(params));

			cmdBindDescriptors(cmd, pRootSignature, descriptorCount, params);
			cmdBindVertexBuffer(cmd, 1, &pSkyBoxVertexBuffer, NULL);
			cmdDraw(cmd, 36, 0);
		}
		cmdEndDebugMarker(cmd);

		{
			//

			{
			DescriptorData floorDrawParameters[MAX_DESCRIPTORS_COUNT] = {};
			
			auto descriptorCount = 0u;

			floorDrawParameters[descriptorCount].pName = "uniformBlock";
			floorDrawParameters[descriptorCount++].ppBuffers = &pProjViewUniformBuffer[gFrameIndex];
			
			floorDrawParameters[descriptorCount].pName = "shadowReceiverUniforms";
			floorDrawParameters[descriptorCount++].ppBuffers = &shadowReceiversUniformBuffer[gFrameIndex];

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
				particlesDescriptors[descriptorCount++].ppBuffers = &pProjViewUniformBuffer[gFrameIndex];

				particlesDescriptors[descriptorCount].pName = "image";
				particlesDescriptors[descriptorCount++].ppTextures = &particlesTexture;

				particlesDescriptors[descriptorCount].pName = "depthBuffer";
				particlesDescriptors[descriptorCount++].ppTextures = &pDepthBuffer->pTexture;

				particlesDescriptors[descriptorCount].pName = "particlesInstances";
				particlesDescriptors[descriptorCount++].ppBuffers = &particlesPerInstanceData[gFrameIndex];

				ASSERT(descriptorCount <= _countof(particlesDescriptors));

				cmdBindPipeline(cmd, particlesPipeline);
				TextureBarrier zReadBarrier[] =
				{
					{ pRenderTarget->pTexture, RESOURCE_STATE_RENDER_TARGET },
					{ pDepthBuffer->pTexture, RESOURCE_STATE_DEPTH_READ | RESOURCE_STATE_SHADER_RESOURCE },
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
		gAppUI.Draw(cmd);
		cmdBindRenderTargets(cmd, 0, nullptr, nullptr, nullptr, nullptr, nullptr, -1, -1);
		cmdEndDebugMarker(cmd);

		{
		TextureBarrier barriers[] = { { pRenderTarget->pTexture, RESOURCE_STATE_PRESENT } };
		cmdResourceBarrier(cmd, 0, nullptr, _countof(barriers), barriers, true);
		}
		endCmd(cmd);

		queueSubmit(pGraphicsQueue, 1, &cmd, pRenderCompleteFence, 1, &pImageAcquiredSemaphore, 1, &pRenderCompleteSemaphore);
		queuePresent(pGraphicsQueue, pSwapChain, gFrameIndex, 1, &pRenderCompleteSemaphore);
	}

	tinystl::string GetName() override
	{
		return "01_Transformations";
	}

	bool addSwapChain()
	{
		SwapChainDesc swapChainDescription = {};
		swapChainDescription.pWindow = pWindow;
		swapChainDescription.mPresentQueueCount = 1;
		swapChainDescription.ppPresentQueues = &pGraphicsQueue;
		swapChainDescription.mWidth = mSettings.mWidth;
		swapChainDescription.mHeight = mSettings.mHeight;
		swapChainDescription.mImageCount = gImageCount;
		swapChainDescription.mSampleCount = SAMPLE_COUNT_1;
		swapChainDescription.mColorFormat = getRecommendedSwapchainFormat(true);
		swapChainDescription.mEnableVsync = false;
		::addSwapChain(pRenderer, &swapChainDescription, &pSwapChain);

		return nullptr != pSwapChain;
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
		addRenderTarget(pRenderer, &depthRenderTargetDescription, &pDepthBuffer);

		return nullptr != pDepthBuffer;
	}

	void recenterCameraView(const float maxDistance)
	{
		const vec3 lookAt = vec3{0};

		vec3 p = pCameraController->getViewPosition();
		vec3 d = p - lookAt;

		const float viewDirectionLengthSqr = lengthSqr(d);
		if (viewDirectionLengthSqr > (maxDistance * maxDistance))
		{
			d *= (maxDistance / sqrtf(viewDirectionLengthSqr));
		}

		p = d + lookAt;
		pCameraController->moveTo(p);
		pCameraController->lookAt(lookAt);
	}

	static bool cameraInputEvent(const ButtonData* const data)
	{
		pCameraController->onInputEvent(data);
		return true;
	}

};

DEFINE_APPLICATION_MAIN(Transformations)