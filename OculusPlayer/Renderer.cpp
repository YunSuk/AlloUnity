#include "Renderer.hpp"

#include "Win32_GLAppUtil.h"
#include <Kernel/OVR_System.h>

// Include the Oculus SDK
#include "OVR_CAPI_GL.h"

using namespace OVR;


Renderer::Renderer(CubemapSource* cubemapSource)
    :
	cubemapSource(cubemapSource), texture(nullptr)
{
	OculusInit();


    std::function<void (CubemapSource*, StereoCubemap*)> callback = boost::bind(&Renderer::onNextCubemap,
                                                                                this,
                                                                                _1,
                                                                                _2);
    cubemapSource->setOnNextCubemap(callback);

	for (int i = 0; i < 1; i++)
	{
		cubemapPool.push(nullptr);
	}

	if (SDL_Init(SDL_INIT_VIDEO/* | SDL_INIT_TIMER*/))
	{
		fprintf(stderr, "Could not initialize SDL - %s\n", SDL_GetError());
		abort();
	}

	/*screen = SDL_CreateWindow("Windowed Player",
		SDL_WINDOWPOS_UNDEFINED,
		SDL_WINDOWPOS_UNDEFINED,
		640, 480,
		0);

	if (!screen) {
		fprintf(stderr, "SDL: could not open window - exiting\n");
		abort;
	}*/

	//Now create a window with title "Hello World" at 100, 100 on the screen with w:640 h:480 and show it
	window = SDL_CreateWindow("Hello World!", 100, 100, 500, 500, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
	//Make sure creating our window went ok
	if (window == nullptr){
		std::cerr << "SDL_CreateWindow Error: " << SDL_GetError() << std::endl;
		abort();
	}

	//Create a renderer that will draw to the window, -1 specifies that we want to load whichever
	//video driver supports the flags we're passing
	//Flags: SDL_RENDERER_ACCELERATED: We want to use hardware accelerated rendering
	//SDL_RENDERER_PRESENTVSYNC: We want the renderer's present function (update screen) to be
	//synchornized with the monitor's refresh rate
	renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (renderer == nullptr){
		SDL_DestroyWindow(window);
		std::cerr << "SDL_CreateRenderer Error: " << SDL_GetError() << std::endl;
		SDL_Quit();
		abort();
	}

	//SDL 2.0 now uses textures to draw things but SDL_LoadBMP returns a surface
	//this lets us choose when to upload or remove textures from the GPU
	//std::string imagePath = getResourcePath("Lesson1") + "hello.bmp";
	/*bmp = SDL_LoadBMP(imagePath.c_str());
	if (bmp == nullptr){
		SDL_DestroyRenderer(renderer);
		SDL_DestroyWindow(window);
		std::cout << "SDL_LoadBMP Error: " << SDL_GetError() << std::endl;
		SDL_Quit();
		abort();
	}*/

}

Renderer::~Renderer()
{
	cubemapBuffer.close();
	cubemapPool.close();
	renderThread.join();

	//Clean up our objects and quit
	SDL_DestroyTexture(texture);
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();


	OculusRelease();
}

void Renderer::onNextCubemap(CubemapSource* source, StereoCubemap* cubemap)
{
	
	StereoCubemap* dummy;
	if (!cubemapPool.wait_and_pop(dummy))
	{
		return;
	}
	cubemapBuffer.push(cubemap);

	void* pixels[12];
	UINT w = cubemap->getEye(0)->getFace(0)->getContent()->getWidth(), h=cubemap->getEye(0)->getFace(0)->getContent()->getWidth();
	for (int e = 0; e < 2; e++){
		for (int i = 0; i < 6; i++){
			pixels[i + 6 * e] = cubemap->getEye(0)->getFace(i)->getContent()->getPixels();	//Draws left eye for both eyes
		}
	}
	
	//scene->updateTextures(pixels, w, h);
	OculusRenderLoop();
}

void Renderer::setOnDisplayedFrame(std::function<void (Renderer*)>& callback)
{
    onDisplayedFrame = callback;
}

void Renderer::setOnDisplayedCubemapFace(std::function<void (Renderer*, int)>& callback)
{
    onDisplayedCubemapFace = callback;
}

void Renderer::OculusInit(){
	hinst = (HINSTANCE)GetModuleHandle(NULL);
	OVR::System::Init();

	// Initialise rift
	if (ovr_Initialize(nullptr) != ovrSuccess) { MessageBoxA(NULL, "Unable to initialize libOVR.", "", MB_OK); return 0; }
	ovrHmd HMD;
	ovrResult result = ovrHmd_Create(0, &HMD);
	if (!OVR_SUCCESS(result))
	{
		result = ovrHmd_CreateDebug(ovrHmd_DK2, &HMD);
	}

	if (!OVR_SUCCESS(result)) { MessageBoxA(NULL, "Oculus Rift not detected.", "", MB_OK); ovr_Shutdown(); return 0; }
	if (HMD->ProductName[0] == '\0') MessageBoxA(NULL, "Rift detected, display not enabled.", "", MB_OK);

	// Setup Window and Graphics
	// Note: the mirror window can be any size, for this sample we use 1/2 the HMD resolution
	ovrSizei windowSize = { HMD->Resolution.w / 2, HMD->Resolution.h / 2 };
	if (!Platform.InitWindowAndDevice(hinst, Recti(Vector2i(0), windowSize), true, L"Oculus Room Tiny (GL)"))
		return 0;

	// Make eye render buffers
	TextureBuffer * eyeRenderTexture[2];
	DepthBuffer   * eyeDepthBuffer[2];
	for (int i = 0; i<2; i++)
	{
		ovrSizei idealTextureSize = ovrHmd_GetFovTextureSize(HMD, (ovrEyeType)i, HMD->DefaultEyeFov[i], 1);
		eyeRenderTexture[i] = new TextureBuffer(HMD, true, true, idealTextureSize, 1, NULL, 1);
		eyeDepthBuffer[i] = new DepthBuffer(eyeRenderTexture[i]->GetSize(), 0);
	}

	// Create mirror texture and an FBO used to copy mirror texture to back buffer
	ovrGLTexture* mirrorTexture;
	ovrHmd_CreateMirrorTextureGL(HMD, GL_RGBA, windowSize.w, windowSize.h, (ovrTexture**)&mirrorTexture);
	// Configure the mirror read buffer
	GLuint mirrorFBO = 0;
	glGenFramebuffers(1, &mirrorFBO);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, mirrorFBO);
	glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mirrorTexture->OGL.TexId, 0);
	glFramebufferRenderbuffer(GL_READ_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

	ovrEyeRenderDesc EyeRenderDesc[2];
	EyeRenderDesc[0] = ovrHmd_GetRenderDesc(HMD, ovrEye_Left, HMD->DefaultEyeFov[0]);
	EyeRenderDesc[1] = ovrHmd_GetRenderDesc(HMD, ovrEye_Right, HMD->DefaultEyeFov[1]);

	ovrHmd_SetEnabledCaps(HMD, ovrHmdCap_LowPersistence | ovrHmdCap_DynamicPrediction);

	// Start the sensor
	ovrHmd_ConfigureTracking(HMD, ovrTrackingCap_Orientation | ovrTrackingCap_MagYawCorrection |
		ovrTrackingCap_Position, 0);

	// Turn off vsync to let the compositor do its magic
	wglSwapIntervalEXT(0);

	// Make scene - can simplify further if needed
	Scene roomScene(false);

	bool isVisible = true;
}

void Renderer::OculusRenderLoop(){
	// Keyboard inputs to adjust player orientation
	static float Yaw(3.141592f);
	if (Platform.Key[VK_LEFT])  Yaw += 0.02f;
	if (Platform.Key[VK_RIGHT]) Yaw -= 0.02f;

	// Keyboard inputs to adjust player position
	static Vector3f Pos2(0.0f, 1.6f, -5.0f);
	if (Platform.Key['W'] || Platform.Key[VK_UP])     Pos2 += Matrix4f::RotationY(Yaw).Transform(Vector3f(0, 0, -0.05f));
	if (Platform.Key['S'] || Platform.Key[VK_DOWN])   Pos2 += Matrix4f::RotationY(Yaw).Transform(Vector3f(0, 0, +0.05f));
	if (Platform.Key['D'])                          Pos2 += Matrix4f::RotationY(Yaw).Transform(Vector3f(+0.05f, 0, 0));
	if (Platform.Key['A'])                          Pos2 += Matrix4f::RotationY(Yaw).Transform(Vector3f(-0.05f, 0, 0));
	Pos2.y = ovrHmd_GetFloat(HMD, OVR_KEY_EYE_HEIGHT, Pos2.y);

	// Animate the cube
	static float cubeClock = 0;
	roomScene.Models[0]->Pos = Vector3f(9 * sin(cubeClock), 3, 9 * cos(cubeClock += 0.015f));

	// Get eye poses, feeding in correct IPD offset
	ovrVector3f               ViewOffset[2] = { EyeRenderDesc[0].HmdToEyeViewOffset,
		EyeRenderDesc[1].HmdToEyeViewOffset };
	ovrPosef                  EyeRenderPose[2];

	ovrFrameTiming   ftiming = ovrHmd_GetFrameTiming(HMD, 0);
	ovrTrackingState hmdState = ovrHmd_GetTrackingState(HMD, ftiming.DisplayMidpointSeconds);
	ovr_CalcEyePoses(hmdState.HeadPose.ThePose, ViewOffset, EyeRenderPose);

	if (isVisible)
	{
		for (int eye = 0; eye<2; eye++)
		{
			// Increment to use next texture, just before writing
			eyeRenderTexture[eye]->TextureSet->CurrentIndex = (eyeRenderTexture[eye]->TextureSet->CurrentIndex + 1) % eyeRenderTexture[eye]->TextureSet->TextureCount;

			// Switch to eye render target
			eyeRenderTexture[eye]->SetAndClearRenderSurface(eyeDepthBuffer[eye]);

			// Get view and projection matrices
			Matrix4f rollPitchYaw = Matrix4f::RotationY(Yaw);
			Matrix4f finalRollPitchYaw = rollPitchYaw * Matrix4f(EyeRenderPose[eye].Orientation);
			Vector3f finalUp = finalRollPitchYaw.Transform(Vector3f(0, 1, 0));
			Vector3f finalForward = finalRollPitchYaw.Transform(Vector3f(0, 0, -1));
			Vector3f shiftedEyePos = Pos2 + rollPitchYaw.Transform(EyeRenderPose[eye].Position);

			Matrix4f view = Matrix4f::LookAtRH(shiftedEyePos, shiftedEyePos + finalForward, finalUp);
			Matrix4f proj = ovrMatrix4f_Projection(HMD->DefaultEyeFov[eye], 0.2f, 1000.0f, ovrProjection_RightHanded);

			// Render world
			roomScene.Render(view, proj);

			// Avoids an error when calling SetAndClearRenderSurface during next iteration.
			// Without this, during the next while loop iteration SetAndClearRenderSurface
			// would bind a framebuffer with an invalid COLOR_ATTACHMENT0 because the texture ID
			// associated with COLOR_ATTACHMENT0 had been unlocked by calling wglDXUnlockObjectsNV.
			eyeRenderTexture[eye]->UnsetRenderSurface();
		}
	}

	// Do distortion rendering, Present and flush/sync

	// Set up positional data.
	ovrViewScaleDesc viewScaleDesc;
	viewScaleDesc.HmdSpaceToWorldScaleInMeters = 1.0f;
	viewScaleDesc.HmdToEyeViewOffset[0] = ViewOffset[0];
	viewScaleDesc.HmdToEyeViewOffset[1] = ViewOffset[1];

	ovrLayerEyeFov ld;
	ld.Header.Type = ovrLayerType_EyeFov;
	ld.Header.Flags = ovrLayerFlag_TextureOriginAtBottomLeft;   // Because OpenGL.

	for (int eye = 0; eye < 2; eye++)
	{
		ld.ColorTexture[eye] = eyeRenderTexture[eye]->TextureSet;
		ld.Viewport[eye] = Recti(eyeRenderTexture[eye]->GetSize());
		ld.Fov[eye] = HMD->DefaultEyeFov[eye];
		ld.RenderPose[eye] = EyeRenderPose[eye];
	}

	ovrLayerHeader* layers = &ld.Header;
	ovrResult result = ovrHmd_SubmitFrame(HMD, 0, &viewScaleDesc, &layers, 1);
	isVisible = OVR_SUCCESS(result);

	// Blit mirror texture to back buffer
	glBindFramebuffer(GL_READ_FRAMEBUFFER, mirrorFBO);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	GLint w = mirrorTexture->OGL.Header.TextureSize.w;
	GLint h = mirrorTexture->OGL.Header.TextureSize.h;
	glBlitFramebuffer(0, h, w, 0,
		0, 0, w, h,
		GL_COLOR_BUFFER_BIT, GL_NEAREST);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

	SwapBuffers(Platform.hDC);
}

void Renderer::OculusRelease(){
	glDeleteFramebuffers(1, &mirrorFBO);
	ovrHmd_DestroyMirrorTexture(HMD, (ovrTexture*)mirrorTexture);
	ovrHmd_DestroySwapTextureSet(HMD, eyeRenderTexture[0]->TextureSet);
	ovrHmd_DestroySwapTextureSet(HMD, eyeRenderTexture[1]->TextureSet);

	// Release
	ovrHmd_Destroy(HMD);
	ovr_Shutdown();
	Platform.ReleaseWindow(hinst);
	OVR::System::Destroy();

}

void Renderer::start()
{

	

	//--------------------------------------------------------------------
	renderThread = boost::thread(boost::bind(&Renderer::renderLoop, this));

	SDL_Event evt;
	while (true)
	{
		SDL_WaitEvent(&evt);
		if (evt.type == SDL_QUIT)
		{
			return;
		}
	}
}

void Renderer::renderLoop()
{
	static int counter = 0;

	while (true)
	{
		StereoCubemap* cubemap;

		if (!cubemapBuffer.wait_and_pop(cubemap))
		{
			return;
		}

		Frame* content = cubemap->getEye(0)->getFace(0)->getContent();

		if (!texture)
		{
			//To use a hardware accelerated texture for rendering we can create one from
			//the surface we loaded
			texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_BGRA8888, SDL_TEXTUREACCESS_STREAMING, content->getWidth(), content->getHeight());
			//We no longer need the surface
			//SDL_FreeSurface(bmp);
			if (texture == nullptr){
				SDL_DestroyRenderer(renderer);
				SDL_DestroyWindow(window);
				std::cerr << "SDL_CreateTextureFromSurface Error: " << SDL_GetError() << std::endl;
				SDL_Quit();
				abort();
			}
		}

		// Show cubemap
		//A sleepy rendering loop, wait for 3 seconds and render and present the screen each time
		//for (int i = 0; i < 3; ++i){
		
		if (counter % 5 == 0)
		{
			void* pixels;
			int   pitch;

			if (SDL_LockTexture(texture, NULL, &pixels, &pitch) < 0)
			{
				SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't lock texture: %s\n", SDL_GetError());
				SDL_Quit();
				abort();
			}

			
			memcpy(pixels, content->getPixels(), content->getHeight() * content->getWidth() * 4);

			SDL_UnlockTexture(texture);


			//First clear the renderer
			SDL_RenderClear(renderer);
			//Draw the texture
			SDL_RenderCopy(renderer, texture, NULL, NULL);
			//Update the screen
			SDL_RenderPresent(renderer);
			//Take a quick break after all that hard work
			//SDL_Delay(1000);

			//}

			if (onDisplayedCubemapFace) onDisplayedCubemapFace(this, 0);
			if (onDisplayedFrame) onDisplayedFrame(this);
		}


		

		StereoCubemap::destroy(cubemap);
		cubemapPool.push(nullptr);
		counter++;
	}
}