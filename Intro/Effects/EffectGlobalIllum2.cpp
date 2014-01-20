﻿#include "../../GodComplex.h"
#include "EffectGlobalIllum2.h"


// Scene selection (also think about chaning the scene in the .RC!)
#define SCENE_PATH	".\\Resources\\Scenes\\GITest1\\ProbeSets\\GITest1_1Probe\\"
//#define SCENE_PATH	".\\Resources\\Scenes\\GITest1\\ProbeSets\\GITest1_10Probes\\"


#define CHECK_MATERIAL( pMaterial, ErrorCode )		if ( (pMaterial)->HasErrors() ) m_ErrorCode = ErrorCode;

#define MAKE_MACROS( a )	Macros( const char* m[] = { a } )

EffectGlobalIllum2::EffectGlobalIllum2( Device& _Device, Texture2D& _RTHDR, Primitive& _ScreenQuad, Camera& _Camera ) : m_ErrorCode( 0 ), m_Device( _Device ), m_RTTarget( _RTHDR ), m_ScreenQuad( _ScreenQuad )
{
	//////////////////////////////////////////////////////////////////////////
	// Create the materials
	D3D_SHADER_MACRO	pMacros[] = { { "USE_SHADOW_MAP", "1" }, { NULL, NULL } };
 	CHECK_MATERIAL( m_pMatRender = CreateMaterial( IDR_SHADER_GI_RENDER_SCENE, "./Resources/Shaders/GIRenderScene2.hlsl", VertexFormatP3N3G3B3T2::DESCRIPTOR, "VS", NULL, "PS", pMacros ), 1 );

	D3D_SHADER_MACRO	pMacros2[] = { { "EMISSIVE", "1" }, { NULL, NULL } };
	CHECK_MATERIAL( m_pMatRenderEmissive = CreateMaterial( IDR_SHADER_GI_RENDER_SCENE, "./Resources/Shaders/GIRenderScene2.hlsl", VertexFormatP3N3G3B3T2::DESCRIPTOR, "VS", NULL, "PS", pMacros2 ), 2 );
 	CHECK_MATERIAL( m_pMatRenderLights = CreateMaterial( IDR_SHADER_GI_RENDER_LIGHTS, "./Resources/Shaders/GIRenderLights.hlsl", VertexFormatP3N3::DESCRIPTOR, "VS", NULL, "PS" ), 3 );
 	CHECK_MATERIAL( m_pMatRenderCubeMap = CreateMaterial( IDR_SHADER_GI_RENDER_CUBEMAP, "./Resources/Shaders/GIRenderCubeMap.hlsl", VertexFormatP3N3G3B3T2::DESCRIPTOR, "VS", NULL, "PS" ), 4 );
 	CHECK_MATERIAL( m_pMatRenderNeighborProbe = CreateMaterial( IDR_SHADER_GI_RENDER_NEIGHBOR_PROBE, "./Resources/Shaders/GIRenderNeighborProbe.hlsl", VertexFormatPt4::DESCRIPTOR, "VS", NULL, "PS" ), 5 );
 	CHECK_MATERIAL( m_pMatRenderShadowMap = CreateMaterial( IDR_SHADER_GI_RENDER_SHADOW_MAP, "./Resources/Shaders/GIRenderShadowMap.hlsl", VertexFormatP3N3G3B3T2::DESCRIPTOR, "VS", NULL, NULL ), 6 );
 	CHECK_MATERIAL( m_pMatPostProcess = CreateMaterial( IDR_SHADER_GI_POST_PROCESS, "./Resources/Shaders/GIPostProcess.hlsl", VertexFormatPt4::DESCRIPTOR, "VS", NULL, "PS" ), 10 );

	// Compute Shaders
 	CHECK_MATERIAL( m_pCSUpdateProbe = CreateComputeShader( IDR_SHADER_GI_UPDATE_PROBE, "./Resources/Shaders/GIUpdateProbe.hlsl", "CS" ), 20 );

m_pCSComputeShadowMapBounds = NULL;	// TODO!


	//////////////////////////////////////////////////////////////////////////
	// Create the textures
	{
		TextureFilePOM	POM( "./Resources/Scenes/GITest1/pata_diff_colo.pom" );
		m_pTexWalls = new Texture2D( _Device, POM );
	}

	// Create the shadow map
	m_pRTShadowMap = new Texture2D( _Device, SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, DepthStencilFormatD32F::DESCRIPTOR );


	//////////////////////////////////////////////////////////////////////////
	// Create the constant buffers
	m_pCB_General = new CB<CBGeneral>( _Device, 9, true );
	m_pCB_Scene = new CB<CBScene>( _Device, 10 );
 	m_pCB_Object = new CB<CBObject>( _Device, 11 );
 	m_pCB_Material = new CB<CBMaterial>( _Device, 12 );
	m_pCB_Probe = new CB<CBProbe>( _Device, 10 );
	m_pCB_Splat = new CB<CBSplat>( _Device, 10 );
	m_pCB_ShadowMap = new CB<CBShadowMap>( _Device, 2, true );
	m_pCB_UpdateProbes = new CB<CBUpdateProbes>( _Device, 11 );

	m_pCB_Scene->m.DynamicLightsCount = 0;
	m_pCB_Scene->m.StaticLightsCount = 0;
	m_pCB_Scene->m.ProbesCount = 0;


	//////////////////////////////////////////////////////////////////////////
	// Create the lights & probes structured buffers
	m_pSB_LightsStatic = new SB<LightStruct>( m_Device, MAX_LIGHTS, true );
	m_pSB_LightsDynamic = new SB<LightStruct>( m_Device, MAX_LIGHTS, true );
	m_pSB_RuntimeProbes = NULL;

	m_pSB_RuntimeProbeUpdateInfos = new SB<RuntimeProbeUpdateInfos>( m_Device, MAX_PROBE_UPDATES_PER_FRAME, true );
	m_pSB_RuntimeProbeSetInfos = new SB<RuntimeProbeUpdateSetInfos>( m_Device, MAX_PROBE_UPDATES_PER_FRAME*MAX_PROBE_SETS, true );
	m_pSB_RuntimeProbeEmissiveSetInfos = new SB<RuntimeProbeUpdateEmissiveSetInfos>( m_Device, MAX_PROBE_UPDATES_PER_FRAME*MAX_PROBE_EMISSIVE_SETS, true );
	m_pSB_RuntimeSamplingPointInfos = new SB<RuntimeSamplingPointInfos>( m_Device, MAX_PROBE_UPDATES_PER_FRAME * MAX_SET_SAMPLES, true );


	//////////////////////////////////////////////////////////////////////////
	// Create the scene
	m_bDeleteSceneTags = false;
	m_TotalFacesCount = 0;
	m_TotalPrimitivesCount = 0;
	m_Scene.Load( IDR_SCENE_GI, *this );

	// Upload static lights once and for all
	m_pSB_LightsStatic->Write( m_pCB_Scene->m.StaticLightsCount );
	m_pSB_LightsStatic->SetInput( 7, true );

	// Update once so it's ready when we pre-compute probes
	m_pCB_Scene->UpdateData();


	//////////////////////////////////////////////////////////////////////////
	// Create our sphere primitive for displaying lights & probes
	m_pPrimSphere = new Primitive( _Device, VertexFormatP3N3::DESCRIPTOR );
	GeometryBuilder::BuildSphere( 40, 10, *m_pPrimSphere );


	//////////////////////////////////////////////////////////////////////////
	// Start precomputation
	PreComputeProbes();


	//////////////////////////////////////////////////////////////////////////
	// Build the ambient sky SH using the CIE overcast sky model
	//
	const int	MAX_THETA = 40;
	double		dPhidTheta = (PI / MAX_THETA) * (2*PI / (2*MAX_THETA));

	double		SumSHCoeffs[9];
	memset( SumSHCoeffs, 0, 9*sizeof(double) );

	double		SHCoeffs[9];
	for ( int ThetaIndex=0; ThetaIndex < MAX_THETA; ThetaIndex++ )
	{
		float	Theta = PI * (0.5f+ThetaIndex) / MAX_THETA;
		for ( int PhiIndex=0; PhiIndex < 2*MAX_THETA; PhiIndex++ )
		{
			float		Phi = PI * PhiIndex / MAX_THETA;
			NjFloat3	Direction( sinf(Phi) * sinf(Theta), cosf(Theta), cosf(Phi)*sinf(Theta) );
			BuildSHCoeffs( Direction, SHCoeffs );

			double		SkyIntensity = SKY_INTENSITY * (1.0f + 2.0f * MAX( -0.5f, cosf(Theta) )) / 3.0f;
			double		SolidAngle = sinf(Theta) * dPhidTheta;

			for ( int i=0; i < 9; i++ )
				SumSHCoeffs[i] += SkyIntensity* SHCoeffs[i] * SolidAngle;
		}
	}

	for ( int i=0; i < 9; i++ )
		m_pSHAmbientSky[i] = float( SumSHCoeffs[i] ) * NjFloat3::One;

}
Texture2D*	pRTCubeMap;

EffectGlobalIllum2::~EffectGlobalIllum2()
{
	delete[] m_pProbes;

	delete m_pPrimSphere;

	m_bDeleteSceneTags = true;
	m_Scene.ClearTags( *this );

	delete m_pSB_RuntimeSamplingPointInfos;
	delete m_pSB_RuntimeProbeEmissiveSetInfos;
	delete m_pSB_RuntimeProbeSetInfos;
	delete m_pSB_RuntimeProbeUpdateInfos;
	delete m_pSB_RuntimeProbes;
	delete m_pSB_LightsDynamic;
	delete m_pSB_LightsStatic;

	delete m_pCB_UpdateProbes;
	delete m_pCB_ShadowMap;
	delete m_pCB_Splat;
	delete m_pCB_Probe;
	delete m_pCB_Material;
	delete m_pCB_Object;
	delete m_pCB_Scene;
	delete m_pCB_General;

	delete m_pRTShadowMap;
	delete m_pTexWalls;

	delete m_pCSUpdateProbe;

	delete m_pMatPostProcess;
	delete m_pCSComputeShadowMapBounds;
	delete m_pMatRenderShadowMap;
	delete m_pMatRenderNeighborProbe;
	delete m_pMatRenderCubeMap;
	delete m_pMatRenderLights;
	delete m_pMatRenderEmissive;
	delete m_pMatRender;

//###
delete pRTCubeMap;
}

// F5 => toggle point light animation
float	AnimateLightTime0 = 0.0f;

// F6 => toggle sun light animation
float	AnimateLightTime1 = 0.0f;
float	UserSunTheta = 60.0f;

// F7 => toggle neon area light animation
float	AnimateLightTime2 = 0.0f;

#define RENDER_SUN	1

void	EffectGlobalIllum2::Render( float _Time, float _DeltaTime )
{
	// Setup general data
	m_pCB_General->m.ShowIndirect = gs_WindowInfos.pKeys[VK_RETURN] == 0;
	m_pCB_General->UpdateData();

	// Setup scene data
	m_pCB_Scene->m.DynamicLightsCount = 1 + RENDER_SUN;
	m_pCB_Scene->m.ProbesCount = m_ProbesCount;
	m_pCB_Scene->UpdateData();


	//////////////////////////////////////////////////////////////////////////
	// Animate lights

		// Point light
	bool	ShowLight0 = !gs_WindowInfos.pKeysToggle[VK_F1];
	if ( ShowLight0 && !gs_WindowInfos.pKeysToggle[VK_F5] )
		AnimateLightTime0 += _DeltaTime;

	if ( ShowLight0 )
		m_pSB_LightsDynamic->m[0].Color.Set( 100, 100, 100 );
	else
		m_pSB_LightsDynamic->m[0].Color.Set( 0, 0, 0 );

	m_pSB_LightsDynamic->m[0].Type = Scene::Light::POINT;
//	m_pSB_LightsDynamic->m[0].Position.Set( 0.0f, 0.2f, 4.0f * sinf( 0.4f * AnimateLightTime0 ) );	// Move along the corridor
	m_pSB_LightsDynamic->m[0].Position.Set( 0.75f * sinf( 1.0f * AnimateLightTime0 ), 0.5f + 0.3f * cosf( 1.0f * AnimateLightTime0 ), 4.0f * sinf( 0.3f * AnimateLightTime0 ) );	// Move along the corridor
	m_pSB_LightsDynamic->m[0].Parms.Set( 0.1f, 0.1f, 0, 0 );


#if RENDER_SUN	// Show Sun light
	{
		bool	ShowLight1 = gs_WindowInfos.pKeysToggle[VK_F2] != 0;
		if ( ShowLight1 && !gs_WindowInfos.pKeysToggle[VK_F6] )
			AnimateLightTime1 += _DeltaTime;

		if ( gs_WindowInfos.pKeys[VK_SUBTRACT] )
			UserSunTheta -= 4.0f * _DeltaTime;
		if ( gs_WindowInfos.pKeys[VK_ADD] )
			UserSunTheta += 4.0f * _DeltaTime;

		float		SunTheta = UserSunTheta * PI / 180.0f;
		float		SunPhi = 0.2f * AnimateLightTime1;
		NjFloat3	SunDirection( sinf(SunTheta) * sinf(SunPhi), cosf(SunTheta), sinf(SunTheta) * cosf(SunPhi) );

		if ( ShowLight1 )
			m_pSB_LightsDynamic->m[1].Color = SUN_INTENSITY * NjFloat3( 1.0f, 0.990f, 0.950f );
		else
			m_pSB_LightsDynamic->m[1].Color.Set( 0, 0, 0 );

		m_pSB_LightsDynamic->m[1].Type = Scene::Light::DIRECTIONAL;
		m_pSB_LightsDynamic->m[1].Direction = SunDirection;
		m_pSB_LightsDynamic->m[1].Parms = NjFloat4::Zero;

		// Render directional shadow map for Sun simulation
		RenderShadowMap( SunDirection );
	}
#else

	// Set shadow map to something otherwise DX pisses me off with warnings...
	m_pCB_ShadowMap->UpdateData();
	m_pRTShadowMap->Set( 2, true );

#endif

	m_pSB_LightsDynamic->Write( 2 );
	m_pSB_LightsDynamic->SetInput( 8, true );


	// Update emissive materials
	if ( m_Scene.m_MaterialsCount > 2 )
	{
		bool	ShowLight2 = gs_WindowInfos.pKeysToggle[VK_F3] != 0;

		if ( ShowLight2 && !gs_WindowInfos.pKeysToggle[VK_F7] )
			AnimateLightTime2 += _DeltaTime;

		if ( ShowLight2 )
		{
//			float	Intensity = 10.0f * MAX( 0.0f, sinf( 4.0f * (AnimateLightTime2 + 0.5f * _frand()) ) );
			float	Intensity = 4.0f * MAX( 0.0f, sinf( 4.0f * (AnimateLightTime2 + 0.0f * _frand()) ) );
			m_Scene.m_ppMaterials[2]->m_EmissiveColor.Set( Intensity * 100, Intensity * 90, Intensity * 70 );
		}
		else
			m_Scene.m_ppMaterials[2]->m_EmissiveColor.Set( 0, 0, 0 );
	}


	//////////////////////////////////////////////////////////////////////////
	// Update dynamic probes
	NjFloat3	pSHAmbient[9];
	if ( gs_WindowInfos.pKeysToggle[VK_F4] )
		memcpy_s( pSHAmbient, sizeof(pSHAmbient), m_pSHAmbientSky, sizeof(m_pSHAmbientSky) );
	else
		memset( pSHAmbient, 0, 9*sizeof(NjFloat3) );

// Ugly "sky"
// double		pTestAmbient[9];
// BuildSHCosineLobe( NjFloat3( -1, 1, 0 ).Normalize(), pTestAmbient );
// for ( int i=0; i < 9; i++ )
// 	pSHAmbient[i] = 100.0f * NjFloat3( 0.7, 0.9, 1.0 ) * float(pTestAmbient[i]);

#if 1
	// Hardware update
	U32		ProbeUpdatesCount = MIN( MAX_PROBE_UPDATES_PER_FRAME, U32(m_ProbesCount) );
//TODO: Handle a stack of probes to update

	// Prepare the buffer of probe update infos and sampling point infos
	int		TotalSamplingPointsCount = 0;
	int		TotalSetsCount = 0;
	int		TotalEmissiveSetsCount = 0;
	for ( U32 ProbeUpdateIndex=0; ProbeUpdateIndex < ProbeUpdatesCount; ProbeUpdateIndex++ )
	{
		int				ProbeIndex = ProbeUpdateIndex;	// Simple at the moment, when we have the update stack we'll have to fetch the index from it...
		ProbeStruct&	Probe = m_pProbes[ProbeIndex];

		// Fill the probe update infos
		RuntimeProbeUpdateInfos&	ProbeUpdateInfos = m_pSB_RuntimeProbeUpdateInfos->m[ProbeUpdateIndex];

		ProbeUpdateInfos.Index = ProbeIndex;
		ProbeUpdateInfos.SetsStart = TotalSetsCount;
		ProbeUpdateInfos.SetsCount = Probe.SetsCount;
		ProbeUpdateInfos.EmissiveSetsStart = TotalEmissiveSetsCount;
		ProbeUpdateInfos.EmissiveSetsCount = Probe.EmissiveSetsCount;
		memcpy_s( ProbeUpdateInfos.SHStatic, sizeof(ProbeUpdateInfos.SHStatic), Probe.pSHBounceStatic, 9*sizeof(NjFloat3) );
		memcpy_s( ProbeUpdateInfos.SHOcclusion, sizeof(ProbeUpdateInfos.SHOcclusion), Probe.pSHOcclusion, 9*sizeof(float) );

		ProbeUpdateInfos.SamplingPointsStart = TotalSamplingPointsCount;

		// Fill the set update infos
		int	SetSamplingPointsCount = 0;
		for ( U32 SetIndex=0; SetIndex < Probe.SetsCount; SetIndex++ )
		{
			ProbeStruct::SetInfos		Set = Probe.pSetInfos[SetIndex];
			RuntimeProbeUpdateSetInfos&	SetUpdateInfos = m_pSB_RuntimeProbeSetInfos->m[TotalSetsCount+SetIndex];

			SetUpdateInfos.SamplingPointsStart = SetSamplingPointsCount;
			SetUpdateInfos.SamplingPointsCount = Set.SamplesCount;
			memcpy_s( SetUpdateInfos.SH, sizeof(SetUpdateInfos.SH), Set.pSHBounce, 9*sizeof(NjFloat3) );

			// Copy sampling points (fortunately it's the same static & runtime structures)
			memcpy_s( &m_pSB_RuntimeSamplingPointInfos->m[TotalSamplingPointsCount], Set.SamplesCount*sizeof(RuntimeSamplingPointInfos), Set.pSamples, Set.SamplesCount*sizeof(ProbeStruct::SetInfos::Sample) );

			TotalSamplingPointsCount += Set.SamplesCount;
			SetSamplingPointsCount += Set.SamplesCount;
		}

		// Fill the emissive set update infos
		for ( U32 EmissiveSetIndex=0; EmissiveSetIndex < Probe.EmissiveSetsCount; EmissiveSetIndex++ )
		{
			ProbeStruct::EmissiveSetInfos		EmissiveSet = Probe.pEmissiveSetInfos[EmissiveSetIndex];
			RuntimeProbeUpdateEmissiveSetInfos&	EmissiveSetUpdateInfos = m_pSB_RuntimeProbeEmissiveSetInfos->m[TotalEmissiveSetsCount+EmissiveSetIndex];

			ASSERT( EmissiveSet.pEmissiveMaterial != NULL, "Invalid emissive material!" );
			EmissiveSetUpdateInfos.EmissiveColor = EmissiveSet.pEmissiveMaterial->m_EmissiveColor;

			memcpy_s( EmissiveSetUpdateInfos.SH, sizeof(EmissiveSetUpdateInfos.SH), EmissiveSet.pSHEmissive, 9*sizeof(float) );
		}

		TotalSetsCount += Probe.SetsCount;
		TotalEmissiveSetsCount += Probe.EmissiveSetsCount;

		ProbeUpdateInfos.SamplingPointsCount = TotalSamplingPointsCount - ProbeUpdateInfos.SamplingPointsStart;	// Total amount of sampling points for the probe
	}

	m_pSB_RuntimeProbeUpdateInfos->Write( ProbeUpdatesCount );
	m_pSB_RuntimeProbeUpdateInfos->SetInput( 10 );

	m_pSB_RuntimeProbeSetInfos->Write( TotalSetsCount );
	m_pSB_RuntimeProbeSetInfos->SetInput( 11 );

	m_pSB_RuntimeProbeEmissiveSetInfos->Write( TotalEmissiveSetsCount );
	m_pSB_RuntimeProbeEmissiveSetInfos->SetInput( 12 );

	m_pSB_RuntimeSamplingPointInfos->Write( TotalSamplingPointsCount );
	m_pSB_RuntimeSamplingPointInfos->SetInput( 13 );

	// Do the update!
	USING_COMPUTESHADER_START( *m_pCSUpdateProbe )

	m_pSB_RuntimeProbes->RemoveFromLastAssignedSlots();
	m_pSB_RuntimeProbes->SetOutput( 0 );

	// Prepare constant buffer for update
	for ( int i=0; i < 9; i++ )
		m_pCB_UpdateProbes->m.AmbientSH[i] = NjFloat4( pSHAmbient[i], 0 );	// Update one by one because of float3 padding
	m_pCB_UpdateProbes->UpdateData();

	M.Dispatch( ProbeUpdatesCount, 1, 1 );

	USING_COMPUTE_SHADER_END

	m_pSB_RuntimeProbes->SetInput( 9, true );

#else
	// Software update (no shadows!)
	for ( int ProbeIndex=0; ProbeIndex < m_ProbesCount; ProbeIndex++ )
	{
		ProbeStruct&	Probe = m_pProbes[ProbeIndex];

		// Clear light accumulation for the probe
		Probe.ClearLightBounce( pSHAmbient );

		// Iterate on each set and compute energy level
		for ( U32 SetIndex=0; SetIndex < Probe.SetsCount; SetIndex++ )
		{
			const ProbeStruct::SetInfos&	Set = Probe.pSetInfos[SetIndex];

			// Compute irradiance for every sample
			NjFloat3	SetIrradiance = NjFloat3::Zero;
			for ( U32 SampleIndex=0; SampleIndex < Set.SamplesCount; SampleIndex++ )
			{
				const ProbeStruct::SetInfos::Sample&	Sample = Set.pSamples[SampleIndex];

				// Compute irradiance from every light
				for ( int LightIndex=0; LightIndex < m_pCB_Scene->m.DynamicLightsCount; LightIndex++ )
				{
					const LightStruct&	Light = m_pSB_LightsDynamic->m[LightIndex];

					// Compute light vector
					NjFloat3	Set2Light = Light.Position - Sample.Position;
					float		DistanceProbe2Light = Set2Light.Length();
					float		InvDistance = 1.0f / DistanceProbe2Light;
					Set2Light = Set2Light * InvDistance;

					float		NdotL = MAX( 0.0f, Set2Light | Sample.Normal );
					NjFloat3	LightIrradiance = Light.Color * NdotL * InvDistance * InvDistance;	// I=E.(N.L)/r²

					SetIrradiance = SetIrradiance + LightIrradiance;
				}
			}

			// Average lighting
			SetIrradiance = SetIrradiance / float(Set.SamplesCount);

			// Transform this into SH
			NjFloat3	pSetSH[9];
			for ( int i=0; i < 9; i++ )
				pSetSH[i] = SetIrradiance * Set.pSHBounce[i];	// Simply irradiance * (Rho/PI) encoded as SH

			// Accumulate to total SH for the probe
			Probe.AccumulateLightBounce( pSetSH );
		}
	}

	// Write to the runtime structured buffer
	for ( int ProbeIndex=0; ProbeIndex < m_ProbesCount; ProbeIndex++ )
	{
		ProbeStruct&	Probe = m_pProbes[ProbeIndex];
		RuntimeProbe&	Runtime = m_pSB_RuntimeProbes->m[ProbeIndex];

		// Write the result to the probe structured buffer
		memcpy( Runtime.pSHBounce, Probe.pSHBouncedLight, 9*sizeof(NjFloat3) );
	}

	m_pSB_RuntimeProbes->Write();
	m_pSB_RuntimeProbes->SetInput( 9, true );

#endif

	//////////////////////////////////////////////////////////////////////////
	// 1] Render the scene
// 	m_Device.ClearRenderTarget( m_RTTarget, NjFloat4::Zero );

 	m_Device.SetRenderTarget( m_RTTarget, &m_Device.DefaultDepthStencil() );
	m_Device.SetStates( m_Device.m_pRS_CullBack, m_Device.m_pDS_ReadWriteLess, m_Device.m_pBS_Disabled );

	m_Scene.Render( *this );

	//////////////////////////////////////////////////////////////////////////
	// 2] Render the lights
	USING_MATERIAL_START( *m_pMatRenderLights )

	m_pPrimSphere->RenderInstanced( M, 1 );	// Only show point light, no sun light

	USING_MATERIAL_END


	//////////////////////////////////////////////////////////////////////////
	// 3] Post-process the result
	USING_MATERIAL_START( *m_pMatPostProcess )

	m_Device.SetStates( m_Device.m_pRS_CullNone, m_Device.m_pDS_Disabled, m_Device.m_pBS_Disabled );
	m_Device.SetRenderTarget( m_Device.DefaultRenderTarget() );

	m_RTTarget.SetPS( 10 );

	m_pCB_Splat->m.dUV = m_Device.DefaultRenderTarget().GetdUV();
	m_pCB_Splat->UpdateData();

	m_ScreenQuad.Render( M );

	USING_MATERIAL_END

	m_RTTarget.RemoveFromLastAssignedSlots();
}


void	EffectGlobalIllum2::ProbeStruct::ClearLightBounce( const NjFloat3 _pSHAmbient[9] )
{
	// 1] Perform the product of direct ambient light with direct environment mask and accumulate with indirect lighting
	NjFloat3	pSHOccludedAmbientLight[9];
	SH::Product3( _pSHAmbient, pSHOcclusion, pSHOccludedAmbientLight );

	// 2] Initialize bounced light with ambient SH + static lighting SH
	for ( int i=0; i < 9; i++ )
		pSHBouncedLight[i] = pSHBounceStatic[i] + pSHOccludedAmbientLight[i];
}

void	EffectGlobalIllum2::ProbeStruct::AccumulateLightBounce( const NjFloat3 _pSHSet[9] )
{
	// Simply accumulate dynamic set lighting to bounced light
	for ( int i=0; i < 9; i++ )
		pSHBouncedLight[i] = pSHBouncedLight[i] + _pSHSet[i];
}

void	EffectGlobalIllum2::PreComputeProbes()
{
	const float		Z_INFINITY = 1e6f;
	const float		Z_INFINITY_TEST = 0.99f * Z_INFINITY;

	pRTCubeMap = new Texture2D( m_Device, CUBE_MAP_SIZE, CUBE_MAP_SIZE, -6 * 3, PixelFormatRGBA32F::DESCRIPTOR, 1, NULL );	// Will contain albedo (cube 0) + (normal + distance) (cube 1) + (static lighting + emissive surface index) (cube 2)
	Texture2D*	pRTCubeMapDepth = new Texture2D( m_Device, CUBE_MAP_SIZE, CUBE_MAP_SIZE, DepthStencilFormatD32F::DESCRIPTOR );

	Texture2D*	pRTCubeMapStaging;
	pRTCubeMapStaging = new Texture2D( m_Device, CUBE_MAP_SIZE, CUBE_MAP_SIZE, -6 * 3, PixelFormatRGBA32F::DESCRIPTOR, 1, NULL, true );		// Will contain albedo


	//////////////////////////////////////////////////////////////////////////
	// Allocate probes
	m_ProbesCount = 0;
	Scene::Node*	pSceneProbe = NULL;
	while ( pSceneProbe = m_Scene.ForEach( Scene::Node::PROBE, pSceneProbe ) )
	{
		m_ProbesCount++;
	}
	m_pProbes = new ProbeStruct[m_ProbesCount];

	pSceneProbe = NULL;
	m_ProbesCount = 0;
	while ( pSceneProbe = m_Scene.ForEach( Scene::Node::PROBE, pSceneProbe ) )
	{
		m_pProbes[m_ProbesCount].pSceneProbe = (Scene::Probe*) pSceneProbe;
		m_ProbesCount++;
	}


	//////////////////////////////////////////////////////////////////////////
	// Prepare the cube map face transforms
	// Here are the transform to render the 6 faces of a cube map
	// Remember the +Z face is not oriented the same way as our Z vector: http://msdn.microsoft.com/en-us/library/windows/desktop/bb204881(v=vs.85).aspx
	//
	//
	//		^ +Y
	//		|   +Z  (our actual +Z faces the other way!)
	//		|  /
	//		| /
	//		|/
	//		o------> +X
	//
	//
	NjFloat3	SideAt[6] = 
	{
		NjFloat3(  1, 0, 0 ),
		NjFloat3( -1, 0, 0 ),
		NjFloat3( 0,  1, 0 ),
		NjFloat3( 0, -1, 0 ),
		NjFloat3( 0, 0,  1 ),
		NjFloat3( 0, 0, -1 ),
	};
	NjFloat3	SideRight[6] = 
	{
		NjFloat3( 0, 0, -1 ),
		NjFloat3( 0, 0,  1 ),
		NjFloat3(  1, 0, 0 ),
		NjFloat3(  1, 0, 0 ),
		NjFloat3(  1, 0, 0 ),
		NjFloat3( -1, 0, 0 ),
	};

	NjFloat4x4	SideWorld2Proj[6];
	NjFloat4x4	Side2Local[6];
	NjFloat4x4	Camera2Proj = NjFloat4x4::ProjectionPerspective( 0.5f * PI, 1.0f, 0.01f, 1000.0f );
	for ( int CubeFaceIndex=0; CubeFaceIndex < 6; CubeFaceIndex++ )
	{
		NjFloat4x4	Camera2Local;
		Camera2Local.SetRow( 0, SideRight[CubeFaceIndex], 0 );
		Camera2Local.SetRow( 1, SideAt[CubeFaceIndex] ^ SideRight[CubeFaceIndex], 0 );
		Camera2Local.SetRow( 2, SideAt[CubeFaceIndex], 0 );
		Camera2Local.SetRow( 3, NjFloat3::Zero, 1 );

		Side2Local[CubeFaceIndex] = Camera2Local;

		NjFloat4x4	Local2Camera = Camera2Local.Inverse();
		NjFloat4x4	Local2Proj = Local2Camera * Camera2Proj;
		SideWorld2Proj[CubeFaceIndex] = Local2Proj;
	}

	// Create the special CB for cube map projections
	struct	CBCubeMapCamera
	{
		NjFloat4x4	Camera2World;
		NjFloat4x4	World2Proj;
	};
	CB<CBCubeMapCamera>*	pCBCubeMapCamera = new CB<CBCubeMapCamera>( m_Device, 9, true );

	//////////////////////////////////////////////////////////////////////////
	// Render every probe as a cube map & process
	//
	for ( int ProbeIndex=0; ProbeIndex < m_ProbesCount; ProbeIndex++ )
//for ( int ProbeIndex=0; ProbeIndex < 1; ProbeIndex++ )
	{
		ProbeStruct&	Probe = m_pProbes[ProbeIndex];

		//////////////////////////////////////////////////////////////////////////
		// 1] Render Albedo + Normal + Distance + Static lit + Emissive Mat ID

		// Clear cube map
		m_Device.ClearRenderTarget( pRTCubeMap->GetTargetView( 0, 0, 6 ), NjFloat4::Zero );
		m_Device.ClearRenderTarget( pRTCubeMap->GetTargetView( 0, 6, 6 ), NjFloat4( 0, 0, 0, Z_INFINITY ) );	// We clear distance to infinity here

		NjFloat4	Bisou = NjFloat4::Zero;
		((U32&) Bisou.w) = 0xFFFFFFFFUL;
		m_Device.ClearRenderTarget( pRTCubeMap->GetTargetView( 0, 12, 6 ), Bisou );

		NjFloat4x4	ProbeLocal2World = Probe.pSceneProbe->m_Local2World;
		ProbeLocal2World.Normalize();

		ASSERT( ProbeLocal2World.GetRow(0).LengthSq() > 0.999f && ProbeLocal2World.GetRow(1).LengthSq() > 0.999f && ProbeLocal2World.GetRow(2).LengthSq() > 0.999f, "Not identity! If not identity then transform probe set positions/normals/etc. by probe matrix!" );

		NjFloat4x4	ProbeWorld2Local = ProbeLocal2World.Inverse();

		// Render the 6 faces
		for ( int CubeFaceIndex=0; CubeFaceIndex < 6; CubeFaceIndex++ )
		{
			// Update cube map face camera transform
			NjFloat4x4	World2Proj = ProbeWorld2Local * SideWorld2Proj[CubeFaceIndex];

			pCBCubeMapCamera->m.Camera2World = Side2Local[CubeFaceIndex] * ProbeLocal2World;
			pCBCubeMapCamera->m.World2Proj = World2Proj;
			pCBCubeMapCamera->UpdateData();

			// Render the scene into the specific cube map faces
			m_Device.SetStates( m_Device.m_pRS_CullFront, m_Device.m_pDS_ReadWriteLess, m_Device.m_pBS_Disabled );

			ID3D11RenderTargetView*	ppViews[3] = {
				pRTCubeMap->GetTargetView( 0, CubeFaceIndex, 1 ),
				pRTCubeMap->GetTargetView( 0, 6+CubeFaceIndex, 1 ),
				pRTCubeMap->GetTargetView( 0, 12+CubeFaceIndex, 1 )
			};
			m_Device.SetRenderTargets( CUBE_MAP_SIZE, CUBE_MAP_SIZE, 3, ppViews, pRTCubeMapDepth->GetDepthStencilView() );

			// Clear depth
			m_Device.ClearDepthStencil( *pRTCubeMapDepth, 1.0f, 0, true, false );

			// Render scene
			Scene::Mesh*	pMesh = NULL;
			while ( pMesh = (Scene::Mesh*) m_Scene.ForEach( Scene::Node::MESH, pMesh ) )
			{
//				if ( pMesh->m_pPrimitives[0].m_pMaterial->m_EmissiveColor.Max() > 1e-4f )

				RenderMesh( *pMesh, m_pMatRenderCubeMap );
			}
		}


		//////////////////////////////////////////////////////////////////////////
		// 2] Read back cube map and create the SH coefficients
		pRTCubeMapStaging->CopyFrom( *pRTCubeMap );

#if 1	// Define this to load probe sets from disk

		char	pTemp[1024];

#if 1	// Save to disk
		sprintf_s( pTemp, SCENE_PATH "Probe%02d.pom", ProbeIndex );
		pRTCubeMapStaging->Save( pTemp );
#endif

		FILE*	pFile = NULL;

		// Read numbered probe
		sprintf_s( pTemp, SCENE_PATH "Probe%02d.probeset", ProbeIndex );
		fopen_s( &pFile, pTemp, "rb" );
		if ( pFile == NULL )
		{	// Not ready yet (happens when first computation!)
			Probe.SetsCount = Probe.EmissiveSetsCount = 0;
			continue;
		}
//		ASSERT( pFile != NULL, "Can't find probeset test file!" );

		// Read the boundary infos
		fread_s( &Probe.MeanDistance, sizeof(Probe.MeanDistance), sizeof(float), 1, pFile );
		fread_s( &Probe.MeanHarmonicDistance, sizeof(Probe.MeanHarmonicDistance), sizeof(float), 1, pFile );
		fread_s( &Probe.MinDistance, sizeof(Probe.MinDistance), sizeof(float), 1, pFile );
		fread_s( &Probe.MaxDistance, sizeof(Probe.MaxDistance), sizeof(float), 1, pFile );

		fread_s( &Probe.BBoxMin.x, sizeof(Probe.BBoxMin.x), sizeof(float), 1, pFile );
		fread_s( &Probe.BBoxMin.y, sizeof(Probe.BBoxMin.y), sizeof(float), 1, pFile );
		fread_s( &Probe.BBoxMin.z, sizeof(Probe.BBoxMin.z), sizeof(float), 1, pFile );
		fread_s( &Probe.BBoxMax.x, sizeof(Probe.BBoxMax.x), sizeof(float), 1, pFile );
		fread_s( &Probe.BBoxMax.y, sizeof(Probe.BBoxMax.y), sizeof(float), 1, pFile );
		fread_s( &Probe.BBoxMax.z, sizeof(Probe.BBoxMax.z), sizeof(float), 1, pFile );

		// Read static SH
		for ( int i=0; i < 9; i++ )
		{
			fread_s( &Probe.pSHBounceStatic[i].x, sizeof(Probe.pSHBounceStatic[i].x), sizeof(float), 1, pFile );
			fread_s( &Probe.pSHBounceStatic[i].y, sizeof(Probe.pSHBounceStatic[i].y), sizeof(float), 1, pFile );
			fread_s( &Probe.pSHBounceStatic[i].z, sizeof(Probe.pSHBounceStatic[i].z), sizeof(float), 1, pFile );
		}

		// Read occlusion SH
		for ( int i=0; i < 9; i++ )
			fread_s( &Probe.pSHOcclusion[i], sizeof(Probe.pSHOcclusion[i]), sizeof(float), 1, pFile );

		// Read the amount of dynamic sets
		U32	SetsCount = 0;
		fread_s( &SetsCount, sizeof(SetsCount), sizeof(U32), 1, pFile );
		Probe.SetsCount = MIN( MAX_PROBE_SETS, SetsCount );	// Don't read more than we can chew!

		// Read the sets
		ProbeStruct::SetInfos	DummySet;
		for ( U32 SetIndex=0; SetIndex < SetsCount; SetIndex++ )
		{
			ProbeStruct::SetInfos&	S = SetIndex < Probe.SetsCount ? Probe.pSetInfos[SetIndex] : DummySet;	// We load into a useless set if out of range...

			// Read position, normal, albedo
			fread_s( &S.Position.x, sizeof(S.Position.x), sizeof(float), 1, pFile );
			fread_s( &S.Position.y, sizeof(S.Position.y), sizeof(float), 1, pFile );
			fread_s( &S.Position.z, sizeof(S.Position.z), sizeof(float), 1, pFile );

			fread_s( &S.Normal.x, sizeof(S.Normal.x), sizeof(float), 1, pFile );
			fread_s( &S.Normal.y, sizeof(S.Normal.y), sizeof(float), 1, pFile );
			fread_s( &S.Normal.z, sizeof(S.Normal.z), sizeof(float), 1, pFile );

			fread_s( &S.Tangent.x, sizeof(S.Tangent.x), sizeof(float), 1, pFile );
			fread_s( &S.Tangent.y, sizeof(S.Tangent.y), sizeof(float), 1, pFile );
			fread_s( &S.Tangent.z, sizeof(S.Tangent.z), sizeof(float), 1, pFile );

			fread_s( &S.BiTangent.x, sizeof(S.BiTangent.x), sizeof(float), 1, pFile );
			fread_s( &S.BiTangent.y, sizeof(S.BiTangent.y), sizeof(float), 1, pFile );
			fread_s( &S.BiTangent.z, sizeof(S.BiTangent.z), sizeof(float), 1, pFile );

			fread_s( &S.Albedo.x, sizeof(S.Albedo.x), sizeof(float), 1, pFile );
			fread_s( &S.Albedo.y, sizeof(S.Albedo.y), sizeof(float), 1, pFile );
			fread_s( &S.Albedo.z, sizeof(S.Albedo.z), sizeof(float), 1, pFile );

			// Transform set's position/normal by probe's LOCAL=>WORLD
			S.Position = NjFloat3( Probe.pSceneProbe->m_Local2World.GetRow(3) ) + S.Position;
// 			NjFloat3	wsSetNormal = Set.Normal;
// 			NjFloat3	wsSetTangent = Set.Tangent;
// 			NjFloat3	wsSetBiTangent = Set.BiTangent;
// TODO: Handle non-identity matrices! Let's go fast for now...
// ARGH! That also means possibly rotating the SH!
// Let's just force the probes to be axis-aligned, shall we??? :) (lazy man talking) (no, seriously, it makes sense after all)

			// Read SH coefficients
			for ( int i=0; i < 9; i++ )
			{
				fread_s( &S.pSHBounce[i].x, sizeof(S.pSHBounce[i].x), sizeof(float), 1, pFile );
				fread_s( &S.pSHBounce[i].y, sizeof(S.pSHBounce[i].y), sizeof(float), 1, pFile );
				fread_s( &S.pSHBounce[i].z, sizeof(S.pSHBounce[i].z), sizeof(float), 1, pFile );
			}

			// Read the samples
			fread_s( &S.SamplesCount, sizeof(S.SamplesCount), sizeof(U32), 1, pFile );
			ASSERT( S.SamplesCount < MAX_SET_SAMPLES, "Too many samples for that set!" );
			for ( U32 SampleIndex=0; SampleIndex < S.SamplesCount; SampleIndex++ )
			{
				ProbeStruct::SetInfos::Sample&	Sample = S.pSamples[SampleIndex];

				// Read position
				fread_s( &Sample.Position.x, sizeof(Sample.Position.x), sizeof(float), 1, pFile );
				fread_s( &Sample.Position.y, sizeof(Sample.Position.y), sizeof(float), 1, pFile );
				fread_s( &Sample.Position.z, sizeof(Sample.Position.z), sizeof(float), 1, pFile );

				// Read normal
				fread_s( &Sample.Normal.x, sizeof(Sample.Normal.x), sizeof(float), 1, pFile );
				fread_s( &Sample.Normal.y, sizeof(Sample.Normal.y), sizeof(float), 1, pFile );
				fread_s( &Sample.Normal.z, sizeof(Sample.Normal.z), sizeof(float), 1, pFile );

				// Read disk radius
				fread_s( &Sample.Radius, sizeof(Sample.Radius), sizeof(float), 1, pFile );


				// Transform set's position/normal by probe's LOCAL=>WORLD
				Sample.Position = NjFloat3( Probe.pSceneProbe->m_Local2World.GetRow(3) ) + Sample.Position;
//				NjFloat3	wsSetNormal = Sample.Normal;
// TODO: Handle non-identity matrices! Let's go fast for now...
// ARGH! That also means possibly rotating the SH!
// Let's just force the probes to be axis-aligned, shall we??? :) (lazy man talking) (no, seriously, it makes sense after all)

			}
		}

		// Read the amount of emissive sets
		U32	EmissiveSetsCount;
		fread_s( &EmissiveSetsCount, sizeof(EmissiveSetsCount), sizeof(U32), 1, pFile );
		Probe.EmissiveSetsCount = MIN( MAX_PROBE_EMISSIVE_SETS, EmissiveSetsCount );	// Don't read more than we can chew!

		// Read the sets
		ProbeStruct::EmissiveSetInfos	DummyEmissiveSet;
		for ( U32 SetIndex=0; SetIndex < EmissiveSetsCount; SetIndex++ )
		{
			ProbeStruct::EmissiveSetInfos&	S = SetIndex < Probe.EmissiveSetsCount ? Probe.pEmissiveSetInfos[SetIndex] : DummyEmissiveSet;	// We load into a useless set if out of range...

			// Read position, normal
			fread_s( &S.Position.x, sizeof(S.Position.x), sizeof(float), 1, pFile );
			fread_s( &S.Position.y, sizeof(S.Position.y), sizeof(float), 1, pFile );
			fread_s( &S.Position.z, sizeof(S.Position.z), sizeof(float), 1, pFile );

			fread_s( &S.Normal.x, sizeof(S.Normal.x), sizeof(float), 1, pFile );
			fread_s( &S.Normal.y, sizeof(S.Normal.y), sizeof(float), 1, pFile );
			fread_s( &S.Normal.z, sizeof(S.Normal.z), sizeof(float), 1, pFile );

			fread_s( &S.Tangent.x, sizeof(S.Tangent.x), sizeof(float), 1, pFile );
			fread_s( &S.Tangent.y, sizeof(S.Tangent.y), sizeof(float), 1, pFile );
			fread_s( &S.Tangent.z, sizeof(S.Tangent.z), sizeof(float), 1, pFile );

			fread_s( &S.BiTangent.x, sizeof(S.BiTangent.x), sizeof(float), 1, pFile );
			fread_s( &S.BiTangent.y, sizeof(S.BiTangent.y), sizeof(float), 1, pFile );
			fread_s( &S.BiTangent.z, sizeof(S.BiTangent.z), sizeof(float), 1, pFile );

			// Read emissive material ID
			U32	EmissiveMatID;
			fread_s( &EmissiveMatID, sizeof(EmissiveMatID), sizeof(U32), 1, pFile );
			ASSERT( EmissiveMatID < U32(m_Scene.m_MaterialsCount), "Material ID out of range!" );
			S.pEmissiveMaterial = m_Scene.m_ppMaterials[EmissiveMatID];

			// Read SH coefficients
			for ( int i=0; i < 9; i++ )
				fread_s( &S.pSHEmissive[i], sizeof(S.pSHEmissive[i]), sizeof(float), 1, pFile );
		}

		fclose( pFile );

#else
		double	dA = 4.0 / (CUBE_MAP_SIZE*CUBE_MAP_SIZE);	// Cube face is supposed to be in [-1,+1], yielding a 2x2 square units
		double	SumSolidAngle = 0.0;

		double	pSHOcclusion[9];
		memset( pSHOcclusion, 0, 9*sizeof(double) );

		for ( int CubeFaceIndex=0; CubeFaceIndex < 6; CubeFaceIndex++ )
		{
			D3D11_MAPPED_SUBRESOURCE&	MappedFaceAlbedo = pRTCubeMapStaging->Map( 0, CubeFaceIndex );
			D3D11_MAPPED_SUBRESOURCE&	MappedFaceGeometry = pRTCubeMapStaging->Map( 0, 6+CubeFaceIndex );

			// Update cube map face camera transform
			NjFloat4x4	Camera2World = Side2Local[CubeFaceIndex] * ProbeLocal2World;

			pCBCubeMapCamera->m.Camera2World = Side2Local[CubeFaceIndex] * ProbeLocal2World;

			NjFloat3	View( 0, 0, 1 );
			for ( int Y=0; Y < CUBE_MAP_SIZE; Y++ )
			{
				NjFloat4*	pScanlineAlbedo = (NjFloat4*) ((U8*) MappedFaceAlbedo.pData + Y * MappedFaceAlbedo.RowPitch);
				NjFloat4*	pScanlineGeometry = (NjFloat4*) ((U8*) MappedFaceGeometry.pData + Y * MappedFaceGeometry.RowPitch);

				View.y = 1.0f - 2.0f * (0.5f + Y) / CUBE_MAP_SIZE;
				for ( int X=0; X < CUBE_MAP_SIZE; X++ )
				{
					NjFloat4	Albedo = *pScanlineAlbedo++;
					NjFloat4	Geometry = *pScanlineGeometry++;

					// Rebuild view direction
					View.x = 2.0f * (0.5f + X) / CUBE_MAP_SIZE - 1.0f;

					// Retrieve the cube map texel's solid angle (from http://people.cs.kuleuven.be/~philip.dutre/GI/TotalCompendium.pdf)
					// dw = cos(Theta).dA / r²
					// cos(Theta) = Adjacent/Hypothenuse = 1/r
					//
					float	SqDistance2Texel = View.LengthSq();
					float	Distance2Texel = sqrtf( SqDistance2Texel );

					double	SolidAngle = dA / (Distance2Texel * SqDistance2Texel);
					SumSolidAngle += SolidAngle;	// CHECK! => Should amount to 4PI at the end of the iteration...

					// Check if we hit an obstacle, in which case we should accumulate direct ambient lighting
					if ( Geometry.w > Z_INFINITY_TEST )
					{	// No obstacle means direct lighting from the ambient sky...
						NjFloat3	ViewWorld = NjFloat4( View, 0.0f ) * Camera2World;	// View vector in world space
						ViewWorld.Normalize();

						// Accumulate SH coefficients in that direction, weighted by the solid angle
 						double	pSHCoeffs[9];
 						BuildSHCoeffs( ViewWorld, pSHCoeffs );
						for ( int i=0; i < 9; i++ )
							pSHOcclusion[i] += SolidAngle * pSHCoeffs[i];

						continue;
					}
				}
			}

			pRTCubeMapStaging->UnMap( 0, CubeFaceIndex );
			pRTCubeMapStaging->UnMap( 0, 6+CubeFaceIndex );
		}

		//////////////////////////////////////////////////////////////////////////
		// 3] Store direct ambient and indirect reflection of static lights on static geometry
		double	Normalizer = 1.0 / SumSolidAngle;
		for ( int i=0; i < 9; i++ )
		{
			Probe.pSHOcclusion[i] = float( Normalizer * pSHOcclusion[i] );

// TODO! At the moment we don't compute static SH coeffs
Probe.pSHBounceStatic[i] = NjFloat3::Zero;
		}

		//////////////////////////////////////////////////////////////////////////
		// 4] Compute solid sets for that probe
		// This part is really important as it will attempt to isolate the important geometric zones near the probe to
		//	approximate them using simple planar impostors that will be lit instead of the entire probe's pixels
		// Each solid set is then lit by dynamic lights in real-time and all pixels belonging to the set add their SH
		//	contribution to the total SH of the probe, this allows us to perform dynamic light bounce on the scene cheaply!
		//


TODO! At the moment we only read back the only pre-computed set from disk

#endif
	}


	//////////////////////////////////////////////////////////////////////////
	// Release
#if 1
m_Device.RemoveRenderTargets();
pRTCubeMap->SetPS( 64 );
#endif

	delete pCBCubeMapCamera;

	delete pRTCubeMapStaging;

	delete pRTCubeMapDepth;

//### Keep it for debugging!
// 	delete pRTCubeMap;


	//////////////////////////////////////////////////////////////////////////
	// Allocate runtime probes structured buffer
	m_pSB_RuntimeProbes = new SB<RuntimeProbe>( m_Device, m_ProbesCount, true );
	for ( int ProbeIndex=0; ProbeIndex < m_ProbesCount; ProbeIndex++ )
	{
		m_pSB_RuntimeProbes->m[ProbeIndex].Position = m_pProbes[ProbeIndex].pSceneProbe->m_Local2World.GetRow( 3 );
		m_pSB_RuntimeProbes->m[ProbeIndex].Radius = m_pProbes[ProbeIndex].MaxDistance;
	}
	m_pSB_RuntimeProbes->Write();
}

//////////////////////////////////////////////////////////////////////////
// Computes the shadow map infos and render the shadow map itself
//
void	EffectGlobalIllum2::RenderShadowMap( const NjFloat3& _SunDirection )
{
	//////////////////////////////////////////////////////////////////////////
	// Build a nice transform
	NjFloat3	X = (NjFloat3::UnitY ^_SunDirection).Normalize();	// Assuming the Sun is never vertical here!
	NjFloat3	Y = _SunDirection ^ X;

	m_pCB_ShadowMap->m.Light2World.SetRow( 0, X );
	m_pCB_ShadowMap->m.Light2World.SetRow( 1, Y );
	m_pCB_ShadowMap->m.Light2World.SetRow( 2, -_SunDirection );
	m_pCB_ShadowMap->m.Light2World.SetRow( 3, NjFloat3::Zero, 1 );	// Temporary

	m_pCB_ShadowMap->m.World2Light = m_pCB_ShadowMap->m.Light2World.Inverse();

	// Find appropriate bounds
	NjFloat3		BBoxMin = 1e6f * NjFloat3::One;
	NjFloat3		BBoxMax = -1e6f * NjFloat3::One;
	Scene::Node*	pMesh = NULL;
	while ( (pMesh = m_Scene.ForEach( Scene::Node::MESH, pMesh )) != NULL )
	{
		NjFloat4x4	Mesh2Light = pMesh->m_Local2World * m_pCB_ShadowMap->m.World2Light;

		// Transform the 8 corners of the mesh's BBox into light space and grow the light's bbox
		const NjFloat3&	MeshBBoxMin = ((Scene::Mesh&) *pMesh).m_BBoxMin;
		const NjFloat3&	MeshBBoxMax = ((Scene::Mesh&) *pMesh).m_BBoxMax;
		for ( int CornerIndex=0; CornerIndex < 8; CornerIndex++ )
		{
			NjFloat3	D;
			D.x = float(CornerIndex & 1);
			D.y = float((CornerIndex >> 1) & 1);
			D.z = float((CornerIndex >> 2) & 1);

			NjFloat3	CornerLocal = MeshBBoxMin + D * (MeshBBoxMax - MeshBBoxMin);
			NjFloat3	CornerLight = NjFloat4( CornerLocal, 1 ) * Mesh2Light;

			BBoxMin = BBoxMin.Min( CornerLight );
			BBoxMax = BBoxMax.Max( CornerLight );
		}
	}

	// Recenter & scale transform accordingly
	NjFloat3	Center = NjFloat4( 0.5f * (BBoxMin + BBoxMax), 1.0f ) * m_pCB_ShadowMap->m.Light2World;	// Center in world space
	NjFloat3	Delta = BBoxMax - BBoxMin;
				Center = Center + 0.5f * Delta.z * _SunDirection;	// Center is now stuck to the bounds' Zmin
	m_pCB_ShadowMap->m.Light2World.SetRow( 3, Center, 1 );

	m_pCB_ShadowMap->m.Light2World.Scale( NjFloat3( 0.5f * Delta.x, 0.5f * Delta.y, Delta.z ) );


	// Finalize constant buffer
	m_pCB_ShadowMap->m.World2Light = m_pCB_ShadowMap->m.Light2World.Inverse();
	m_pCB_ShadowMap->m.BoundsMin = BBoxMin;
	m_pCB_ShadowMap->m.BoundsMax = BBoxMax;

	m_pCB_ShadowMap->UpdateData();



//CHECK => All corners should be in [(-1,-1,0),(+1,+1,1)]
// BBoxMin = 1e6f * NjFloat3::One;
// BBoxMax = -1e6f * NjFloat3::One;
// pMesh = NULL;
// while ( (pMesh = m_Scene.ForEach( Scene::Node::MESH, pMesh )) != NULL )
// {
// 	NjFloat4x4	Mesh2Light = pMesh->m_Local2World * m_pCB_ShadowMap->m.World2Light;
// 
// 	// Transform the 8 corners of the mesh's BBox into light space and grow the light's bbox
// 	const NjFloat3&	MeshBBoxMin = ((Scene::Mesh&) *pMesh).m_BBoxMin;
// 	const NjFloat3&	MeshBBoxMax = ((Scene::Mesh&) *pMesh).m_BBoxMax;
// 	for ( int CornerIndex=0; CornerIndex < 8; CornerIndex++ )
// 	{
// 		NjFloat3	D;
// 		D.x = float(CornerIndex & 1);
// 		D.y = float((CornerIndex >> 1) & 1);
// 		D.z = float((CornerIndex >> 2) & 1);
// 
// 		NjFloat3	CornerLocal = MeshBBoxMin + D * (MeshBBoxMax - MeshBBoxMin);
// 		NjFloat3	CornerLight = NjFloat4( CornerLocal, 1 ) * Mesh2Light;
// 
// 		BBoxMin = BBoxMin.Min( CornerLight );
// 		BBoxMax = BBoxMax.Max( CornerLight );
// 	}
// }
//CHECK


	//////////////////////////////////////////////////////////////////////////
	// Perform actual rendering
	USING_MATERIAL_START( *m_pMatRenderShadowMap )

	m_Device.SetStates( m_Device.m_pRS_CullNone, m_Device.m_pDS_ReadWriteLess, m_Device.m_pBS_Disabled );

	m_pRTShadowMap->RemoveFromLastAssignedSlots();
	m_Device.ClearDepthStencil( *m_pRTShadowMap, 1.0f, 0, true, false );
	m_Device.SetRenderTargets( m_pRTShadowMap->GetWidth(), m_pRTShadowMap->GetHeight(), 0, NULL, m_pRTShadowMap->GetDepthStencilView() );

	Scene::Node*	pMesh = NULL;
	while ( (pMesh = m_Scene.ForEach( Scene::Node::MESH, pMesh )) != NULL )
	{
		RenderMesh( (Scene::Mesh&) *pMesh, &M );
	}

	USING_MATERIAL_END

	// Assign the shadow map to shaders
	m_Device.RemoveRenderTargets();
	m_pRTShadowMap->Set( 2, true );
}


//////////////////////////////////////////////////////////////////////////
// Spherical Harmonics Helpers

// Builds the 9 SH coefficient for the specified direction
// (We're already accounting for the fact we're Y-up here)
//
void	EffectGlobalIllum2::BuildSHCoeffs( const NjFloat3& _Direction, double _Coeffs[9] )
{
	const double	f0 = 0.5 / sqrt(PI);
	const double	f1 = sqrt(3.0) * f0;
	const double	f2 = sqrt(15.0) * f0;
	const double	f3 = sqrt(5.0) * 0.5 * f0;

	_Coeffs[0] = f0;
	_Coeffs[1] = -f1 * _Direction.x;
	_Coeffs[2] = f1 * _Direction.y;
	_Coeffs[3] = -f1 * _Direction.z;
	_Coeffs[4] = f2 * _Direction.x * _Direction.z;
	_Coeffs[5] = -f2 * _Direction.x * _Direction.y;
	_Coeffs[6] = f3 * (3.0 * _Direction.y*_Direction.y - 1.0);
	_Coeffs[7] = -f2 * _Direction.z * _Direction.y;
	_Coeffs[8] = f2 * 0.5 * (_Direction.z*_Direction.z - _Direction.x*_Direction.x);
}

// Builds a spherical harmonics cosine lobe
// (from "Stupid SH Tricks")
// (We're already accounting for the fact we're Y-up here)
//
void	EffectGlobalIllum2::BuildSHCosineLobe( const NjFloat3& _Direction, double _Coeffs[9] )
{
	const NjFloat3 ZHCoeffs = NjFloat3(
		0.88622692545275801364908374167057f,	// sqrt(PI) / 2
		1.0233267079464884884795516248893f,		// sqrt(PI / 3)
		0.49541591220075137666812859564002f		// sqrt(5PI) / 8
		);
	ZHRotate( _Direction, ZHCoeffs, _Coeffs );
}

// Builds a spherical harmonics cone lobe (same as for a spherical light source subtending a cone of half angle a)
// (from "Stupid SH Tricks")
//
void	EffectGlobalIllum2::BuildSHCone( const NjFloat3& _Direction, float _HalfAngle, double _Coeffs[9] )
{
	double	a = _HalfAngle;
	double	c = cos( a );
	double	s = sin( a );
	NjFloat3 ZHCoeffs = NjFloat3(
			float( 1.7724538509055160272981674833411 * (1 - c)),				// sqrt(PI) (1 - cos(a))
			float( 1.5349900619197327327193274373339 * (s * s)),				// 0.5 sqrt(3PI) sin(a)^2
			float( 1.9816636488030055066725143825601 * (c * (1 - c) * (1 + c)))	// 0.5 sqrt(5PI) cos(a) (1-cos(a)) (cos(a)+1)
		);
	ZHRotate( _Direction, ZHCoeffs, _Coeffs );
}

// Builds a spherical harmonics smooth cone lobe
// The light source intensity is 1 at theta=0 and 0 at theta=half angle
// (from "Stupid SH Tricks")
//
void	EffectGlobalIllum2::BuildSHSmoothCone( const NjFloat3& _Direction, float _HalfAngle, double _Coeffs[9] )
{
	double	a = _HalfAngle;
	float	One_a3 = 1.0f / float(a*a*a);
	double	c = cos( a );
	double	s = sin( a );
	NjFloat3 ZHCoeffs = One_a3 * NjFloat3(
			float( 1.7724538509055160272981674833411 * (a * (6.0*(1+c) + a*a) - 12*s) ),					// sqrt(PI) (a^3 + 6a - 12*sin(a) + 6*cos(a)*a) / a^3
			float( 0.76749503095986636635966371866695 * (a * (a*a + 3*c*c) - 3*c*s) ),						// 0.25 sqrt(3PI) (a^3 - 3*cos(a)*sin(a) + 3*cos(a)^2*a) / a^3
			float( 0.44036969973400122370500319612446 * (-6.0*a -2*c*c*s -9.0*c*a + 14.0*s + 3*c*c*c*a))	// 1/9 sqrt(5PI) (-6a - 2*cos(a)^2*sin(a) - 9*cos(a)*a + 14*sin(a) + 3*cos(a)^3*a) / a^3
		);
	ZHRotate( _Direction, ZHCoeffs, _Coeffs );
}

// Rotates ZH coefficients in the specified direction (from "Stupid SH Tricks")
// Rotating ZH comes to evaluating scaled SH in the given direction.
// The scaling factors for each band are equal to the ZH coefficients multiplied by sqrt( 4PI / (2l+1) )
//
void	EffectGlobalIllum2::ZHRotate( const NjFloat3& _Direction, const NjFloat3& _ZHCoeffs, double _Coeffs[9] )
{
	double	cl0 = 3.5449077018110320545963349666823 * _ZHCoeffs.x;	// sqrt(4PI)
	double	cl1 = 2.0466534158929769769591032497785 * _ZHCoeffs.y;	// sqrt(4PI/3)
	double	cl2 = 1.5853309190424044053380115060481 * _ZHCoeffs.z;	// sqrt(4PI/5)

	double	f0 = cl0 * 0.28209479177387814347403972578039;	// 0.5 / sqrt(PI);
	double	f1 = cl1 * 0.48860251190291992158638462283835;	// 0.5 * sqrt(3.0/PI);
	double	f2 = cl2 * 1.0925484305920790705433857058027;	// 0.5 * sqrt(15.0/PI);
	_Coeffs[0] = f0;
	_Coeffs[1] = -f1 * _Direction.x;
	_Coeffs[2] = f1 * _Direction.y;
	_Coeffs[3] = -f1 * _Direction.z;
	_Coeffs[4] = f2 * _Direction.x * _Direction.z;
	_Coeffs[5] = -f2 * _Direction.x * _Direction.y;
	_Coeffs[6] = f2 * 0.28209479177387814347403972578039 * (3.0 * _Direction.y*_Direction.y - 1.0);
	_Coeffs[7] = -f2 * _Direction.z * _Direction.y;
	_Coeffs[8] = f2 * 0.5f * (_Direction.z*_Direction.z - _Direction.x*_Direction.x);
}

//////////////////////////////////////////////////////////////////////////
// Scene Rendering
//
void*	EffectGlobalIllum2::TagMaterial( const Scene& _Owner, const Scene::Material& _Material )
{
	if ( m_bDeleteSceneTags )
	{
		return NULL;
	}

	switch ( _Material.m_ID )
	{
	case 0:
	case 1:
	case 2:

		if ( _Material.m_EmissiveColor.Max() > 1e-4f )
			return m_pMatRenderEmissive;	// Special emissive materials!

		return m_pMatRender;

	default:
		ASSERT( false, "Unsupported material!" );
	}
	return NULL;
}
void*	EffectGlobalIllum2::TagTexture( const Scene& _Owner, const Scene::Material::Texture& _Texture )
{
	if ( m_bDeleteSceneTags )
	{
		return NULL;
	}

	switch ( _Texture.m_ID )
	{
	case 0:		return m_pTexWalls;
	case ~0:	return NULL;	// Invalid textures are not mapped
	default:
		ASSERT( false, "Unsupported texture!" );
	}
	return NULL;
}

void*	EffectGlobalIllum2::TagNode( const Scene& _Owner, const Scene::Node& _Node )
{
	if ( m_bDeleteSceneTags )
	{
		return NULL;
	}

	if ( _Node.m_Type == Scene::Node::LIGHT )
	{	// Add another static light
		Scene::Light&	SourceLight = (Scene::Light&) _Node;
		LightStruct&	TargetLight = m_pSB_LightsStatic->m[m_pCB_Scene->m.StaticLightsCount++];

		TargetLight.Type = SourceLight.m_LightType;
		TargetLight.Position = SourceLight.m_Local2Parent.GetRow( 3 );
		TargetLight.Direction = -SourceLight.m_Local2Parent.GetRow( 2 ).Normalize();
		TargetLight.Color = SourceLight.m_Intensity * SourceLight.m_Color;
		TargetLight.Parms.Set( 10.0f, 11.0f, cosf( SourceLight.m_HotSpot ), cosf( SourceLight.m_Falloff ) );
	}

	return NULL;
}

void*	EffectGlobalIllum2::TagPrimitive( const Scene& _Owner, const Scene::Mesh& _Mesh, const Scene::Mesh::Primitive& _Primitive )
{
	if ( m_bDeleteSceneTags )
	{	// Delete the primitive
		Primitive*	pPrim = (Primitive*) _Primitive.m_pTag;	// We need to cast it as a primitive first so the destructor gets called
		delete pPrim;
		return NULL;
	}

	// Create an actual rendering primitive
	IVertexFormatDescriptor*	pVertexFormat = NULL;
	switch ( _Primitive.m_VertexFormat )
	{
	case Scene::Mesh::Primitive::P3N3G3B3T2:	pVertexFormat = &VertexFormatP3N3G3B3T2::DESCRIPTOR;
	}
	ASSERT( pVertexFormat != NULL, "Unsupported vertex format!" );

	Primitive*	pPrim = new Primitive( m_Device, _Primitive.m_VerticesCount, _Primitive.m_pVertices, 3*_Primitive.m_FacesCount, _Primitive.m_pFaces, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, *pVertexFormat );

	// Tag the primitive with the face offset
	pPrim->m_pTag = (void*) m_TotalFacesCount;
	m_pPrimitiveFaceOffset[m_TotalPrimitivesCount++] = m_TotalFacesCount;	// Store face offset for each primitive
	m_TotalFacesCount += pPrim->GetFacesCount();							// Increase total amount of faces

	return pPrim;
}

void	EffectGlobalIllum2::RenderMesh( const Scene::Mesh& _Mesh, Material* _pMaterialOverride )
{
	// Upload the object's CB
	memcpy( &m_pCB_Object->m.Local2World, &_Mesh.m_Local2World, sizeof(NjFloat4x4) );
	m_pCB_Object->UpdateData();

	for ( int PrimitiveIndex=0; PrimitiveIndex < _Mesh.m_PrimitivesCount; PrimitiveIndex++ )
	{
		Scene::Mesh::Primitive&	ScenePrimitive = _Mesh.m_pPrimitives[PrimitiveIndex];
		Scene::Material&		SceneMaterial = *ScenePrimitive.m_pMaterial;

		Material*	pMat = _pMaterialOverride == NULL ? (Material*) SceneMaterial.m_pTag : _pMaterialOverride;
		if ( pMat == NULL )
			continue;	// Unsupported material!
		Primitive*	pPrim = (Primitive*) ScenePrimitive.m_pTag;
		if ( pPrim == NULL )
			continue;	// Unsupported primitive!

		// Upload textures
		Texture2D*	pTexDiffuseAlbedo = (Texture2D*) SceneMaterial.m_TexDiffuseAlbedo.m_pTag;
		if ( pTexDiffuseAlbedo != NULL )
			pTexDiffuseAlbedo->SetPS( 10 );
		Texture2D*	pTexSpecularAlbedo = (Texture2D*) SceneMaterial.m_TexSpecularAlbedo.m_pTag;
		if ( pTexSpecularAlbedo != NULL )
			pTexSpecularAlbedo->SetPS( 11 );

		// Upload the primitive's material CB
		m_pCB_Material->m.ID = SceneMaterial.m_ID;
		m_pCB_Material->m.DiffuseAlbedo = SceneMaterial.m_DiffuseAlbedo;
		m_pCB_Material->m.HasDiffuseTexture = pTexDiffuseAlbedo != NULL;
		m_pCB_Material->m.SpecularAlbedo = SceneMaterial.m_SpecularAlbedo;
		m_pCB_Material->m.HasSpecularTexture = pTexSpecularAlbedo != NULL;
		m_pCB_Material->m.EmissiveColor = SceneMaterial.m_EmissiveColor;
		m_pCB_Material->m.SpecularExponent = SceneMaterial.m_SpecularExponent.x;
		m_pCB_Material->m.FaceOffset = U32(pPrim->m_pTag);
		m_pCB_Material->UpdateData();

		int		Test = sizeof(CBMaterial);

		// Render
		pMat->Use();
		pPrim->Render( *pMat );
	}
}