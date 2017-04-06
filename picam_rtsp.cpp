#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

#include <bcm_host.h>
#include <interface/vcos/vcos.h>
#include <IL/OMX_Broadcom.h>
#include <pthread.h>
#include <iostream>

#include "omx_dump.hh"
#include "omx_utils.hh"
#include "piRtspServer.hh"

int main (int argc, char** argv)
{
    bool verbose = 0;
    int width = 640;
    int height = 480;
    int fps = 30;
    int bitrate = 3000000;
    char *captureUrl = "0.0.0.0";

    int c = 0;
    while ((c = getopt (argc, argv, "vhW:H:r:f:b:")) != -1)
    {
        switch (c)
        {
            case 'v':   verbose = 1; break;
            case 'W':   width = atoi(optarg); break;
            case 'H':   height = atoi(optarg); break;
            case 'r':   captureUrl = optarg; break;           
            case 'f':   fps = atoi(optarg); break;
            case 'b':   bitrate = atoi(optarg); break;
            case 'h':
            {
                std::cout << argv[0] << " [-v] [-W width] [-H height]" << std::endl;
                std::cout << "\t -v            : verbose " << std::endl;
                std::cout << "\t -W width      : capture width (default "<< width << ")" << std::endl;
                std::cout << "\t -H height     : capture height (default "<< height << ")" << std::endl;
                std::cout << "\t -f fps        : capture framerate (default "<< fps << ")" << std::endl;
                std::cout << "\t -b bitrate    : capture bitrate (default "<< bitrate << ")" << std::endl;
                std::cout << "\t -r url        : rtsp url (default " << captureUrl << ")" << std::endl;
                exit(0);
            }
        }
    }

    OMX_ERRORTYPE error;
    OMX_BUFFERHEADERTYPE* encoder_output_buffer;
    component_t camera;
    component_t encoder;
    component_t null_sink;
    
    camera.name = "OMX.broadcom.camera";
    encoder.name = "OMX.broadcom.video_encode";
    null_sink.name = "OMX.broadcom.null_sink";
    
    // Initialize Broadcom's VideoCore APIs
    bcm_host_init ();
    
    // Initialize OpenMAX IL
    if ((error = OMX_Init ())) {
        fprintf (stderr, "error: OMX_Init: %s\n", dump_OMX_ERRORTYPE (error));
        exit (1);
    }
    
    // Initialize components
    init_component (&camera);
    init_component (&encoder);
    init_component (&null_sink);
    
    // Initialize camera drivers
    load_camera_drivers (&camera);
    
    // Configure camera port definition
    printf ("configuring %s port definition\n", camera.name);
    OMX_PARAM_PORTDEFINITIONTYPE port_st;
    OMX_INIT_STRUCTURE (port_st);
    port_st.nPortIndex = 71;
    error = OMX_GetParameter (camera.handle,
                              OMX_IndexParamPortDefinition, &port_st);
    if (error) {
        fprintf (stderr, "error: OMX_GetParameter: %s\n",
                 dump_OMX_ERRORTYPE (error));
        exit (1);
    }
    port_st.format.video.nFrameWidth = width;
    port_st.format.video.nFrameHeight = height;
    port_st.format.video.nStride = width;
    port_st.format.video.xFramerate = fps << 16;
    port_st.format.video.eCompressionFormat = OMX_VIDEO_CodingUnused;
    port_st.format.video.eColorFormat = OMX_COLOR_FormatYUV420PackedPlanar;
    error = OMX_SetParameter (camera.handle,
                              OMX_IndexParamPortDefinition, &port_st);
    if (error) {
        fprintf (stderr, "error: OMX_SetParameter: %s\n",
                 dump_OMX_ERRORTYPE (error));
        exit (1);
    }
    
    // Same config to the preview port
    port_st.nPortIndex = 70;
    error = OMX_SetParameter (camera.handle,
                              OMX_IndexParamPortDefinition, &port_st);
    if (error) {
        fprintf (stderr, "error: OMX_SetParameter: %s\n",
                 dump_OMX_ERRORTYPE (error));
        exit (1);
    }
    
    // Camera framerate
    printf ("configuring %s framerate\n", camera.name);
    OMX_CONFIG_FRAMERATETYPE framerate_st;
    OMX_INIT_STRUCTURE (framerate_st);
    framerate_st.nPortIndex = 71;
    framerate_st.xEncodeFramerate = port_st.format.video.xFramerate;
    error = OMX_SetConfig (camera.handle, OMX_IndexConfigVideoFramerate,
                           &framerate_st);
    if (error) {
        fprintf (stderr, "error: OMX_SetConfig: %s\n",
                 dump_OMX_ERRORTYPE (error));
        exit (1);
    }
    
    // Configure camera
    // camera_settings (&camera);
    
    // Configure encoder port definitions
    printf ("configuring %s port definition\n", encoder.name);
    OMX_INIT_STRUCTURE (port_st);
    port_st.nPortIndex = 201;
    error = OMX_GetParameter (encoder.handle,
                              OMX_IndexParamPortDefinition, &port_st);
    if (error) {
        fprintf (stderr, "error: OMX_GetParameter: %s\n",
                 dump_OMX_ERRORTYPE (error));
        exit (1);
    }
    port_st.format.video.nFrameWidth = width;
    port_st.format.video.nFrameHeight = height;
    port_st.format.video.nStride = width;
    port_st.format.video.xFramerate = fps << 16;
    //Despite being configured later, these two fields need to be set
    port_st.format.video.nBitrate = VIDEO_QP ? 0 : bitrate;
    port_st.format.video.eCompressionFormat = OMX_VIDEO_CodingAVC;
    error = OMX_SetParameter (encoder.handle, OMX_IndexParamPortDefinition,
                              &port_st);
    if (error) {
        fprintf (stderr, "error: OMX_SetParameter: %s\n",
                 dump_OMX_ERRORTYPE (error));
        exit (1);
    }
    
    // Configure encoder
    encoder_settings (&encoder);
    
    // Setup tunnels:
    // camera (video 7) -> video_encode
    // camera (preview) -> null_sink
    printf ("configuring tunnels\n");
    error = OMX_SetupTunnel (camera.handle, 71, encoder.handle, 200);
    if (error) {
        fprintf (stderr, "error: OMX_SetupTunnel: %s\n",
                 dump_OMX_ERRORTYPE (error));
        exit (1);
    }
    error = OMX_SetupTunnel (camera.handle, 70, null_sink.handle, 240);
    if (error) {
        fprintf (stderr, "error: OMX_SetupTunnel: %s\n",
                 dump_OMX_ERRORTYPE (error));
        exit (1);
    }
    
    //Change state to IDLE
    change_state (&camera, OMX_StateIdle);
    wait (&camera, EVENT_STATE_SET, 0);
    change_state (&encoder, OMX_StateIdle);
    wait (&encoder, EVENT_STATE_SET, 0);
    change_state (&null_sink, OMX_StateIdle);
    wait (&null_sink, EVENT_STATE_SET, 0);

    //Enable the ports
    enable_port (&camera, 71);
    wait (&camera, EVENT_PORT_ENABLE, 0);
    enable_port (&camera, 70);
    wait (&camera, EVENT_PORT_ENABLE, 0);
    enable_port (&null_sink, 240);
    wait (&null_sink, EVENT_PORT_ENABLE, 0);
    enable_port (&encoder, 200);
    wait (&encoder, EVENT_PORT_ENABLE, 0);
    enable_encoder_output_port (&encoder, &encoder_output_buffer);

    //Change state to EXECUTING
    change_state (&camera, OMX_StateExecuting);
    wait (&camera, EVENT_STATE_SET, 0);
    change_state (&encoder, OMX_StateExecuting);
    wait (&encoder, EVENT_STATE_SET, 0);
    wait (&encoder, EVENT_PORT_SETTINGS_CHANGED, 0);
    change_state (&null_sink, OMX_StateExecuting);
    wait (&null_sink, EVENT_STATE_SET, 0);

    //Enable camera capture port. This basically says that the port 71 will be
    //used to get data from the camera. If you're capturing a still, the port 72
    //must be used
    printf ("enabling %s capture port\n", camera.name);
    OMX_CONFIG_PORTBOOLEANTYPE capture_st;
    OMX_INIT_STRUCTURE (capture_st);
    capture_st.nPortIndex = 71;
    capture_st.bEnabled = OMX_TRUE;
    error = OMX_SetConfig (camera.handle, OMX_IndexConfigPortCapturing,
                           &capture_st);
    if (error) {
        fprintf (stderr, "error: OMX_SetConfig: %s\n", dump_OMX_ERRORTYPE (error));
        exit (1);
    }
    
    // Prepare rtsp server
    pthread_t server_thread;
    PI_MEMORY_BUFFER* rtsp_buffer =
        new PI_MEMORY_BUFFER (1000000/fps);
    
    
	// Create rtsp thread
    struct rtsp_server_params serverParams;
    serverParams.rtsp_buffer = rtsp_buffer;
    serverParams.captureUrl = captureUrl;
    int ret = pthread_create (&server_thread, NULL,
                            &startRtspServer, &serverParams);
    if (ret != 0) {
        fprintf (stderr, "error: pthread_create: %d\n", ret);
        exit (1);
    }
    

    // Capturing loop
    while (1) {
        // Get the buffer data
        error = OMX_FillThisBuffer (encoder.handle, encoder_output_buffer);
        if (error) {
            fprintf (stderr, "error: OMX_FillThisBuffer: %s\n",
                     dump_OMX_ERRORTYPE (error));
            exit (1);
        }
        
        // Wait until it's filled
        wait (&encoder, EVENT_FILL_BUFFER_DONE, 0);
        
        // Write to the rtsp buffer
		//printf ("write frame %d\n", encoder_output_buffer->nFilledLen);
        rtsp_buffer->push_frame_data (encoder_output_buffer->pBuffer,
            encoder_output_buffer->nFilledLen);
    }
    
    // Disable camera capture port
    printf ("disabling %s capture port\n", camera.name);
    capture_st.bEnabled = OMX_FALSE;
    error = OMX_SetConfig (camera.handle, OMX_IndexConfigPortCapturing,
                           &capture_st);
    if (error) {
        fprintf (stderr, "error: OMX_SetConfig: %s\n", dump_OMX_ERRORTYPE (error));
        exit (1);
    }
    
    // Change state to IDLE
    change_state (&camera, OMX_StateIdle);
    wait (&camera, EVENT_STATE_SET, 0);
    change_state (&encoder, OMX_StateIdle);
    wait (&encoder, EVENT_STATE_SET, 0);
    change_state (&null_sink, OMX_StateIdle);
    wait (&null_sink, EVENT_STATE_SET, 0);
    
    // Disable the tunnel ports
    disable_port (&camera, 71);
    wait (&camera, EVENT_PORT_DISABLE, 0);
    disable_port (&camera, 70);
    wait (&camera, EVENT_PORT_DISABLE, 0);
    disable_port (&null_sink, 240);
    wait (&null_sink, EVENT_PORT_DISABLE, 0);
    disable_port (&encoder, 200);
    wait (&encoder, EVENT_PORT_DISABLE, 0);
    disable_encoder_output_port (&encoder, encoder_output_buffer);
    
    // Change state to LOADED
    change_state (&camera, OMX_StateLoaded);
    wait (&camera, EVENT_STATE_SET, 0);
    change_state (&encoder, OMX_StateLoaded);
    wait (&encoder, EVENT_STATE_SET, 0);
    change_state (&null_sink, OMX_StateLoaded);
    wait (&null_sink, EVENT_STATE_SET, 0);
    
    // Deinitialize components
    deinit_component (&camera);
    deinit_component (&encoder);
    deinit_component (&null_sink);

    // Deinitialize OpenMAX IL
    if ((error = OMX_Deinit ())) {
        fprintf (stderr, "error: OMX_Deinit: %s\n", dump_OMX_ERRORTYPE (error));
        exit (1);
    }
    
    // Deinitialize Broadcom's VideoCore APIs
    bcm_host_deinit ();

    printf ("Done!\n");

	return 0;
}
