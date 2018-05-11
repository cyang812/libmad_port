#include "mad.h"
#include <stdio.h>
#include <stdint.h>

/*
 * 分散读取MP3文件到内存，以减少内存消耗
 */

#define mp3_file_path "E:\\test.mp3"
#define pcm_file_path "E:\\test.pcm"

//for debug
//#define DEBUG_INFO
static uint32_t d_num = 0;
static uint32_t read_num = 0;

static struct mad_stream *Stream;
static struct mad_frame  *Frame;
static struct mad_synth  *Synth;

unsigned char	*ReadStart = NULL;
unsigned char	*GuardPtr = NULL;

unsigned char   *mp3_in_buf = NULL;         // use malloc 
#define         MP3_IN_BUF_LEN   4096

unsigned short  *pcm_out_buf = NULL;
#define         PCM_OUT_BUF_LEN  2304*2
unsigned short	*OutputPtr = NULL;          // use malloc 

static unsigned char 	layer2_state = 0;
static long     ReadSize, Remaining;			

static FILE *mp3_file, *pcm_file;

int get_data(char *buf, int size)
{
    int ret = 0;

    ret = fread(buf, sizeof(char), size, mp3_file);

    return ret;
}

int write_data(char *buf, int size)
{
    int ret = 0;

    ret = fwrite(buf, sizeof(char), size, pcm_file);

    return ret;
}

static inline signed int scale(mad_fixed_t sample)
{
  /* round */
  sample += (1L << (MAD_F_FRACBITS - 16));

  /* clip */
  if (sample >= MAD_F_ONE)
	sample = MAD_F_ONE - 1;
  else if (sample < -MAD_F_ONE)
	sample = -MAD_F_ONE;

  /* quantize */
  return sample >> (MAD_F_FRACBITS + 1 - 16);
}

int Init_layer2_Decoder()
{
    Stream = (struct mad_stream*)malloc(sizeof(struct mad_stream));
    if(Stream == NULL)
        return -1;

	Frame = (struct mad_frame*)malloc(sizeof(struct mad_frame));
    if(Frame == NULL)
    {
        free(Stream);
        Stream = NULL;
        return -1;
    }

	Synth = (struct mad_synth*)malloc(sizeof(struct mad_synth));
    if(Synth == NULL)
    {
        free(Stream);
        free(Frame);
        Stream = NULL;
        Frame = NULL;
        return -1;
    }    

	mad_stream_init(Stream);
	mad_frame_init(Frame);
	mad_synth_init(Synth);
	ReadStart = NULL;
	GuardPtr = NULL;

    mp3_in_buf = (uint8_t *)malloc(MP3_IN_BUF_LEN);
    if(mp3_in_buf == NULL)
    {
        free(Stream);
        free(Frame);
        free(Synth);
        Stream = NULL;
        Frame = NULL;
        Synth = NULL;
        return -1;
    }
    
	pcm_out_buf = (uint16_t *)malloc(PCM_OUT_BUF_LEN); 
	if(pcm_out_buf != NULL)
	{
		OutputPtr = (uint16_t *)&pcm_out_buf[0];
	}
	else
	{
        free(Stream);
        free(Frame);
        free(Synth);
        free(mp3_in_buf);    
        Stream = NULL;
        Frame = NULL;
        Synth = NULL;
        mp3_in_buf = NULL;
		printf("malloc pcm out buffer fail!\n");
		return -1;	
	}

	layer2_state = 0;

	return 0;
}

void Close_layer2_Decoder()
{
	mad_synth_finish(Synth);
	mad_frame_finish(Frame);
	mad_stream_finish(Stream);
	
	if(Stream != NULL)
	{
		free(Stream);
		Stream = NULL;
	}

	if(Frame != NULL)
	{
		free(Frame);
		Frame = NULL;
	}

	if(Synth != NULL)
	{
		free(Synth);
		Synth = NULL;
	}

    if(mp3_in_buf == NULL)
    {
        free(mp3_in_buf);
        mp3_in_buf = NULL;
    }

	if(pcm_out_buf != NULL)
	{
		free(pcm_out_buf);
		pcm_out_buf = NULL;
	}
}

int exe_layer2_Decoder()
{
	switch (layer2_state)
	{
		//init
		case 0x00:
			if(Stream->buffer==NULL || Stream->error==MAD_ERROR_BUFLEN)
			{
				if(Stream->next_frame != NULL)
				{
					Remaining = Stream->bufend - Stream->next_frame;
					memmove(mp3_in_buf, Stream->next_frame, Remaining);
					ReadStart = mp3_in_buf + Remaining;
					ReadSize = MP3_IN_BUF_LEN - Remaining;
				}
				else
				{
					ReadSize  = MP3_IN_BUF_LEN;
					ReadStart = mp3_in_buf;
					Remaining = 0;
				}

				/* feed data to decoder */
//				printf("read data to decoder\n");
				ReadSize = get_data(ReadStart, ReadSize);    
                if(!ReadSize)
                {
                    layer2_state = 0x30; //end
                    printf("decode end!\n");
                    break;
                }
				
				mad_stream_buffer(Stream, mp3_in_buf, ReadSize+Remaining);
	  			Stream->error = MAD_ERROR_NONE;
				layer2_state = 0x10;
			}
			else 
			{   
			    #ifdef DEBUG_INFO
				if(Stream->error != MAD_ERROR_NONE)
				{
					printf("[MAD ERR1 ##### d=%d,r=%d] = %#x\n",d_num, read_num, Stream->error);
				}
				#endif
				layer2_state = 0x10;
			}
			break;

		//decode 
		case 0x10:
			if(mad_frame_decode(Frame,Stream))
			{
			    #ifdef DEBUG_INFO
				if(Stream->error != MAD_ERROR_BUFLEN)
				{
					printf("[MAD ERR2 ##### d=%d,r=%d] = %#x\n",d_num, read_num, Stream->error);
				}
				#endif
				
				if(MAD_RECOVERABLE(Stream->error))
				{
					//continue;
					layer2_state = 0x00;
				}
				else 
				{
					if(Stream->error==MAD_ERROR_BUFLEN)
					{
						//continue;
						layer2_state = 0x00;	
					}
					else
					{
						//when you didn't init the mp2 decoder
						printf("unrecoverable frame level error %d.\r\n",Stream->error);
						return 0;//fail
					}
				}
			}
			else //decode succ
			{
				layer2_state = 0x20;
			}
			break;

		//write 
		case 0x20:
			d_num++;
		
			mad_synth_frame(Synth,Frame);

			/* Synthesized samples must be converted from libmad's fixed
			 * point number to the consumer format. Here we use unsigned
			 * 16 bit big endian integers on two channels. Integer samples
			 * are temporarily stored in a buffer that is flushed when
			 * full.
			 */

			// format and copy dec internel's data to pcm out data
			if(MAD_NCHANNELS(&Frame->header) == 2)
			{
				for(int i=0; i<Synth->pcm.length; i++)
				{
					*(OutputPtr++)= scale(Synth->pcm.samples[0][i]);
					*(OutputPtr++)= scale(Synth->pcm.samples[1][i]);
				}
			}
			else
			{
				for(int i=0; i<Synth->pcm.length; i++)
				{
					*(OutputPtr++) = scale(Synth->pcm.samples[0][i]);
					//*(OutputPtr++) = 0;  //mono
					*(OutputPtr++) = scale(Synth->pcm.samples[0][i]);  //stereo
				}
			}

            write_data((uint8_t *)pcm_out_buf, PCM_OUT_BUF_LEN);
			
			OutputPtr = pcm_out_buf;
			layer2_state = 0x00;
			break;

        case 0x30:
            return 0;//end
            break;

        default:
            break;
	}

	return 1;
}


int main(int argc, char * argv [ ])
{
    printf("main begin!\n");

    mp3_file = fopen(mp3_file_path, "rb");
    pcm_file = fopen(pcm_file_path, "wb");
    if(!mp3_file || !pcm_file)
    {
        printf("file err!\n");
        return 0;
    }

    Init_layer2_Decoder();

    while(exe_layer2_Decoder());

    Close_layer2_Decoder();

    fclose(mp3_file);
    fclose(pcm_file);

    printf("main end!\n");

    return 0;
}
