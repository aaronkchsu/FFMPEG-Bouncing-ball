#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

#include <stdio.h>

void save(AVFrame *frame, int width, int height, int num)
{
  AVCodec *codec;
  AVCodecContext *context;
  AVPacket packet;
  
  FILE *file;  
  char file_name[32];
  int ones, tens, hundreds, output;
  
  // Sets up number format
  ones = num % 10;
  num = num - ones;

  tens = num % 100;
  num = num - tens;
  tens = tens / 10;

  hundreds = num % 1000;
  hundreds = hundreds / 100;
  
  // Prepare the file
  sprintf(file_name, "frame%d%d%d.mpff", hundreds, tens, ones); // Creates the file name according to the number
  file = fopen(file_name,"wb");

  // Find the codec for MPFF and allocate the context
  codec = avcodec_find_encoder(AV_CODEC_ID_MPFF);
  context = avcodec_alloc_context3(codec);
  
  // Set context variables
  context->width = width;
  context->height = height;
  context->pix_fmt = codec->pix_fmts[0];
  
  // Open the codec (taken from Shawn Van Every example)
  // http://ffmpeg.org/pipermail/libav-user/2012-December/003378.html
  avcodec_open2(context, codec, NULL);

  frame->format = codec->pix_fmts[0];
  frame->width = width;
  frame->height = height;
  
  // Initialize the packet to hold the frame
  av_init_packet(&packet);
  packet.data = NULL; // packet data will be allocated by the encoder
  packet.size = 0; 
  
  // Encode the frame in the MPFF format (from Shawn Van Every example)
  avcodec_encode_video2(context, &packet, frame, &output);

  if(output)
    {
      // Write the bytes of the packet to the file
      fwrite(packet.data, 1, packet.size, file);
    }
  
  // Free and close resources
  av_free_packet(&packet);
  fclose(file);
  avcodec_close(context);
  av_free(context);
}

/*
 * This method draws a ball on the frame, according to the x and y positions given
 */
void drawBouncie(AVFrame *frame, int width, int height, int y_pos, int x_pos, int radius, int bytes)
{
  int dist, x, y;
  int y_col, x_col, x_shade, y_shade, shade;
  int b, position, pix_width;
  int y_diff, x_diff, dist_diff;

  // Set the maximum distance for coloring
  dist = radius * radius;
  
  // Set the width of the image in pixels
  pix_width = width / bytes;

  // Set the x and y color origin
  y_col = y_pos - radius / 2;
  x_col = x_pos - radius / 2;

  // Loop through all of the pixels
  for(y = (y_pos - radius); y <= (y_pos + radius); y++)
    {
      for(x = (x_pos - radius); x <= (x_pos + radius); x++)
	{
	  y_diff = (y - y_pos) * (y - y_pos);
	  x_diff = (x - x_pos) * (x - x_pos);
	  dist_diff = y_diff + x_diff;
	  
	  if(dist_diff <= dist)
	    {
	      // Determine how far from the "light source" we are
	      y_shade = (y - y_col) * (y - y_col);
	      x_shade = (x - x_col) * (x - x_col);

	      // Shade accordingly
	      shade = 0xff - (sqrt(y_shade + x_shade) * 150 / radius);
	      if(shade < 0)
		shade = 0;

	      // Get the position of the pixel
	      position = (y * width + (x * bytes));

	      // Update the byte(s) of the pixel in the frame data
	      if(bytes == 1)
		frame->data[0][position] = shade;
	      else
		{
		  frame->data[0][position] = 0xff;
		  for(b = 1; b < bytes; b++)
		    frame->data[0][position + b] = shade;
		}
	    }
	}
    }
}

int main(int argc, char *argv[])
{
  AVFormatContext *format_ctx = NULL; // Gets the format ctx aka jpg mpff will have their own type
  AVCodecContext *codec_ctx = NULL;
  AVCodecContext *context = NULL;
  AVCodec *codec = NULL;
  AVCodec *ecodec = NULL;
  AVFrame *frame = NULL; // Two frames are used to do the conversion
  AVFrame *frame_rgb = NULL;
  AVFrame *frame_copy = NULL;
  AVPacket packet; // Returns the data that we need to write the file

  struct SwsContext *sws_ctx = NULL;
  uint8_t *buffer = NULL;  
  int height, width, num_bytes, num, frame_finished, y_pos, velocity, radius, temp, bytes, accel, x_pos, x_change;
  const char *file_name;  
  const char *ext;

  // Ensure an argument
  if(argc < 2)
    {
      printf("Please provide a jpeg file\n");
      return -1;
    }

  file_name = argv[1]; // This should be the name of the picture file
  ext = strrchr(file_name, '.'); // Gets the extention of the file name
  
  // Check the extension
  if(ext == NULL || !(ext == ".jpg" || ext == ".jpeg" || ext == ".jpe" || ext == ".jfif" || ext != ".jif"))
    {
      printf("Please privide a jpeg file\n");
      return -1;
    }
  
  // Where designated, parts are taken from the dranger tutorial on decoding ffmpeg frames
  // https://github.com/chelyaev/ffmpeg-tutorial

  // Register all formats
  av_register_all();
  
  // The following was taken from dranger
  // {
  // Open the file
  avformat_open_input(&format_ctx, file_name, NULL, NULL);
  
  // Retrieve stream information
  avformat_find_stream_info(format_ctx, NULL);
  
  // Dump information about file onto standard error
  av_dump_format(format_ctx, 0, file_name, 0);
  
  // Get a pointer to the codec context and find the decoder
  codec_ctx = format_ctx->streams[0]->codec;
  // }

  // Find the decoder
  codec = avcodec_find_decoder(codec_ctx->codec_id);
  ecodec = avcodec_find_encoder(AV_CODEC_ID_MPFF);
  
  // Open the codec
  avcodec_open2(codec_ctx, codec, NULL);

  // Set the height and width  
  height = codec_ctx->height;
  width = codec_ctx->width;

  // In order to scale down the orginal jpg into a rbg format which the mpff is in we need two frames we store the original info in the first frame and then convert it
  frame = av_frame_alloc();
  frame_rgb = av_frame_alloc();
  
  frame_rgb->width = width;
  frame_rgb->height = height;
  frame_rgb->format = ecodec->pix_fmts[0];
  
  // Determine required buffer size and allocate buffer
  // Also taken from dranger
  // {
  num_bytes = avpicture_get_size(ecodec->pix_fmts[0], width, height);
  buffer = (uint8_t *) av_malloc(num_bytes * sizeof(uint8_t));
  
  // Associate our allocated buffer with the allocated frame
  avpicture_fill((AVPicture *) frame_rgb, buffer,  ecodec->pix_fmts[0], width, height);
  
  // Set the scaling context (new frame will be the SAME SIZE, but converted to RBG8)
  sws_ctx = sws_getContext(width, height, codec_ctx->pix_fmt,
			   width, height,  ecodec->pix_fmts[0],
			   SWS_BILINEAR, NULL, NULL, NULL);
  // }
  
  // Read and decode the frame
  av_read_frame(format_ctx, &packet);
  avcodec_decode_video2(codec_ctx, frame, &frame_finished, &packet);
  
  // Scale the image to RGB
  // Taken from dranger
  sws_scale(sws_ctx, (uint8_t const * const *)frame->data, frame->linesize, 0, height,
	    frame_rgb->data, frame_rgb->linesize);

  // Set frame and ball variables
  num = 0;
  radius = height / 10;
  y_pos = height / 4;
  x_pos = width / 4;
  temp = y_pos;

  // Set horizontal velocity
  x_change = width / 100;
  if(x_change < 1)
    x_change = 1;

  // Set vertical velocity and acceleration
  velocity = 0;
  accel = height / 200;
  if(accel < 1)
    accel = 1;

  // Find the codec for MPFF and allocate the context
  codec = avcodec_find_encoder(AV_CODEC_ID_MPFF);
  context = avcodec_alloc_context3(codec);
  
  // Set context variables
  context->width = width;
  context->height = height;
  context->pix_fmt = codec->pix_fmts[0];
  
  // Open the codec
  avcodec_open2(context, codec, NULL);
  bytes = (context->bits_per_coded_sample) / 8;

  while(num < 300)
    {
      // Make a copy of the frame
      frame_copy = av_frame_clone(frame_rgb);

      // Update the y position
      temp = temp + velocity;
      if(temp > (height - radius))
	{
	  y_pos = height - radius;
	  velocity = 0 - velocity - accel;
	}
      else
	{
	  y_pos = temp;
	}      

      // Draw a ball at a certain position depending on the frame number
      drawBouncie(frame_copy, frame_copy->linesize[0], height, y_pos, x_pos, radius, bytes);
      
      // Save the frame with a certain frame number
      save(frame_copy, width, height, num);
      
      // Free the frame copy
      av_free(frame_copy);

      // Update the horizontal and vertical velocity
      velocity = velocity + accel;
      x_pos = x_pos + x_change;
      if(x_pos > (width - radius))
	{
	  x_pos = width - radius;
	  x_change = 0 - x_change;
	}
      else if(x_pos < radius)
	{
	  x_pos = radius;
	  x_change = 0 - x_change;
	}

      // Update the frame number
      num = num + 1;
    }
  
  // Taken from dranger
  // {
  // Free the packet
  av_free_packet(&packet); 
  
  // Free the buffer and the frames
  av_free(buffer);
  av_free(frame_rgb);
  av_free(frame);
  
  // Close the codec
  avcodec_close(codec_ctx);
  
  // Close the video file
  avformat_close_input(&format_ctx);
  // }
  
  return 0;
}
