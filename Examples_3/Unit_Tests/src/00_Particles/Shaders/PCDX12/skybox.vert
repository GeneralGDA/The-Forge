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

// Shader for Skybox in Unit Test 01 - Transformations

cbuffer ProjectionUniforms : register(b0)
{
    float4x4 mvp;
	float4x4 camera;

	float4 zProjection; // x <- scale, y <- bias
};

struct VertexShaderOutput 
{
	float4 Position : SV_POSITION;
    float4 TexCoord : TEXCOORD;
};

VertexShaderOutput main(float4 worldSpacePosition : POSITION)
{
	VertexShaderOutput result;
 
    float4 projected = float4(worldSpacePosition.xyz * 9.0, 1.0);
    projected = mul(mvp, projected);

    result.Position = projected.xyww;
    result.TexCoord = worldSpacePosition;

	return result;
}
