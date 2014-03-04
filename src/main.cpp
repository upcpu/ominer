/*
This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

Author: Jim Liu
2013-2014
*/

#include "CL\cl.h"
//#include "utils.h"
#include "sha2.h"

#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <windows.h>

#ifdef _MSC_VER
#include <intrin.h>
#define BSWAP64(x) (_byteswap_uint64((uint64_t)(x)))
#else
#define BSWAP64(x) (__builtin_bswap64((uint64_t)(x)))
#endif


static cl_mem g_inputBuffer = NULL;
static cl_mem g_result = NULL;
static cl_mem g_offset = NULL;
static cl_mem g_matchBuffer = NULL;
static cl_mem g_midhash = NULL;
static cl_uint g_device_num = 0;
cl_context	g_context = NULL;
cl_command_queue g_cmd_queue = NULL;
cl_program	g_program = NULL;
cl_kernel	g_kernel = NULL;
cl_kernel	g_birthday_kernel = NULL;
cl_kernel	g_ready_kernel = NULL;
cl_kernel	g_match_kernel = NULL;

static cl_uint g_platform_num = 0;
bool g_amd_GPU = false;
bool g_nv_GPU = false;

unsigned int g_total_found = 0;
unsigned int g_total_ignored = 0;

//for perf. counters
#include <Windows.h>

#define NONCE_BITS 26
#define MAX_GPU_NUM 32
#define SEARCH_SPACE_BITS		50
#define BIRTHDAYS_PER_HASH		8
#define CACHED_HASHES			(32)

#define NONCE_MASK  (0xffffffffffffffff << (64-SEARCH_SPACE_BITS))
#define LOOKUP_BITS 27
#define VAL_MASK (0xffffffff <<  LOOKUP_BITS)


#define CONFLICT_MAP_SIZE	g_conflict_map_size
#define MATCH_ARRAY_SIZE 0x200000
#define RESULT_ARRAY_SIZE 2048
#define MID_HASH_BUF_SIZE (16 * sizeof(uint64_t))

#define MAX_MATCH_PAIR_SIZE 0x4FFFF
#define MAX_FOUND_IN_TURN 128



static unsigned int g_conflict_map_size = 0;
static unsigned g_group_shift = 20;
static unsigned g_group_size = 4;


enum gpu_algos {
    AUTO,
	GEEKJ,		/* GCN*/
	KISS,		/* CLASSIC*/
	GEN,
};

static const char *gpu_algo_names[] = {
    [AUTO] = "auto",
	[GEEKJ] = "GeekJ",
	[KISS] = "Kiss",
	[GEN] = "gen"
};


unsigned g_work_size = 64;
unsigned g_run_turns = 2;
bool g_dbg_flag = false;
unsigned int g_stat_every_turns = 8;
enum gpu_algos g_algo = AUTO;

LARGE_INTEGER g_PerfFrequency;
LARGE_INTEGER g_PerfCPUStart;
LARGE_INTEGER g_PerfCPUStop;

LARGE_INTEGER g_PerfTotalStart;
LARGE_INTEGER g_PerfTotalStop;

LARGE_INTEGER g_PerformanceCountNDRangeStart;
LARGE_INTEGER g_PerformanceCountNDRangeStop;


void Cleanup_OpenCL()
{
    if( g_inputBuffer ) {clReleaseMemObject( g_inputBuffer ); g_inputBuffer = NULL;}
    //if( g_inputBuffer2 ) {clReleaseMemObject( g_inputBuffer2 ); g_inputBuffer2 = NULL;}
    if( g_offset ) {clReleaseMemObject( g_offset ); g_offset = NULL;}
    if( g_midhash ) {clReleaseMemObject( g_midhash ); g_midhash = NULL;}
    if( g_kernel ) {clReleaseKernel( g_kernel ); g_kernel = NULL;}
    if( g_birthday_kernel){clReleaseKernel( g_birthday_kernel ); g_birthday_kernel = NULL;}
    if( g_match_kernel){clReleaseKernel( g_match_kernel ); g_match_kernel = NULL;}
    if( g_program ) {clReleaseProgram( g_program ); g_program = NULL;}
    if( g_cmd_queue ) {clReleaseCommandQueue( g_cmd_queue ); g_cmd_queue = NULL;}
    if( g_context ) {clReleaseContext( g_context ); g_context = NULL;}
}

const char* getclErrString(cl_int errcode);



static cl_platform_id pPlatforms[10] = { 0 };
const char* getclErrString(cl_int errcode);

cl_platform_id GetOCLPlatform(const cl_uint platform_num)
{
    char pPlatformName[256] = { 0 };

    cl_uint uiPlatformsCount = 0;
    cl_int err = clGetPlatformIDs(10, pPlatforms, &uiPlatformsCount);

    if ( err != CL_SUCCESS ) {
        printf("ERROR[%d]: Failed to get opencl platform ids . (%s) \n",
               err, getclErrString(err));
        return NULL;
    }
    else{
        printf("Found opencl %d platform(s), selected: %d\n", uiPlatformsCount, platform_num);
    }

    for (cl_uint ui = 0; ui < uiPlatformsCount; ++ui)
    {
        err = clGetPlatformInfo(pPlatforms[ui], CL_PLATFORM_NAME,
                                sizeof(pPlatformName) - 1, pPlatformName, NULL);
        if ( err != CL_SUCCESS ) {
            printf("ERROR: Failed to retreive platform %d name.\n", ui);
            return NULL;
        }

        printf("Found platform: %s\n", pPlatformName);

        if(ui==platform_num){
            printf("Select platform [%d]: %s\n", ui, pPlatformName);
            return pPlatforms[ui];

        }

    }

    return NULL;
}

void BuildFailLog( cl_program program,
                  cl_device_id device_id )
{
    size_t paramValueSizeRet = 0;
    clGetProgramBuildInfo(program, device_id, CL_PROGRAM_BUILD_LOG, 0, NULL, &paramValueSizeRet);

    char* buildLogMsgBuf = (char *)malloc(sizeof(char) * paramValueSizeRet + 1);
		clGetProgramBuildInfo(program, device_id, CL_PROGRAM_BUILD_LOG, paramValueSizeRet, buildLogMsgBuf, &paramValueSizeRet);
		buildLogMsgBuf[paramValueSizeRet] = '\0';	//mark end of message string

		printf("Build Log:\n");
		puts(buildLogMsgBuf);
		fflush(stdout);

		free(buildLogMsgBuf);
}


char *ReadSources(const char *fileName)
{
    FILE *file = fopen(fileName, "rb");
    if (!file)
    {
        printf("ERROR: Failed to open file '%s'\n", fileName);
        return NULL;
    }

    if (fseek(file, 0, SEEK_END))
    {
        printf("ERROR: Failed to seek file '%s'\n", fileName);
		fclose(file);
        return NULL;
    }

    long size = ftell(file);
    if (size == 0)
    {
        printf("ERROR: Failed to check position on file '%s'\n", fileName);
		fclose(file);
        return NULL;
    }

    rewind(file);

    char *src = (char *)malloc(sizeof(char) * size + 1 + 16384/*jim patch*/);
    if (!src)
    {
        printf("ERROR: Failed to allocate memory for file '%s'\n", fileName);
		fclose(file);
        return NULL;
    }

    printf("Reading file '%s' (size %ld bytes)\n", fileName, size);
    size_t res = fread(src, 1, sizeof(char) * size, file);
    if (res != sizeof(char) * size)
    {
        printf("ERROR: Failed to read momentum kernel file.\n");//, fileName);
		fclose(file);
		free(src);
        return NULL;
    }

    src[size] = '\n';
    src[size + 1]  = 0;
    fclose(file);

    return src;
}


extern "C" int Setup_OpenCL( const char *program_source,
                cl_uint* alignment,
                 const unsigned int map_size,
                 unsigned int vector_width,
                 const unsigned int gpu_algo,
                 const unsigned int platform_num,
                 const unsigned int dev_num);

int Setup_OpenCL(const char *program_source, cl_uint* alignment,
                 const unsigned int map_size,
                 const unsigned int gpu_algo,
                 const unsigned int platform_num, const unsigned int dev_num,
                 const int is_output_build_log)
{
    g_conflict_map_size = map_size;
    g_algo = AUTO;

    switch(gpu_algo){
    case 0:
        g_algo = AUTO;
        break;
    case 1:
        g_algo = GEEKJ;
        break;
    case 2:
        g_algo = KISS;
        break;
    case 3:
        g_algo = GEN;
        break;
    }

    g_group_size = 4;
    g_device_num = dev_num;

    cl_device_id devices[MAX_GPU_NUM];
    //cl_uint size_ret = 0;
    cl_int err;
	cl_platform_id ocl_platform_id = GetOCLPlatform(platform_num/*dev_num*/);
    if( ocl_platform_id == NULL ){
        printf("ERROR: Failed to find available OpenCL platform.\n");
        return -1;
    }

    char ocl_version_info[256] = { 0 };
    err = clGetPlatformInfo(ocl_platform_id, CL_PLATFORM_VERSION,
                                sizeof(ocl_version_info)-1, ocl_version_info, NULL);
    if ( err != CL_SUCCESS ) {
        printf("ERROR[%d]: Failed to retreive platform %d's Opencl version info.\n", err, platform_num);
        return -1;
    }
    else{
        printf("Opencl version of platform %d: %s\n", platform_num, ocl_version_info);
    }

    /*cl_context_properties context_properties[3] = {CL_CONTEXT_PLATFORM,
                (cl_context_properties)ocl_platform_id, 1 };
                */

    cl_uint             numDevices = 0;
    cl_uint             numGPUDevices = 0;

    clGetDeviceIDs(ocl_platform_id, CL_DEVICE_TYPE_GPU, 0, NULL, &numDevices);
    if(numDevices == 0)    //no GPU available.
    {
        puts("Error: No any GPU devices available in platform! Only GPU is supported now.\n");
        return 1;
    }
    else{
        err = clGetDeviceIDs(ocl_platform_id, CL_DEVICE_TYPE_GPU, numDevices,
                             devices, &numGPUDevices);
        if (err != CL_SUCCESS) {
            printf("ERROR[%d]: Failed to get GPU device's ids . (%s) \n",
               err, getclErrString(err));
        return -1;
       }
       printf("The MAX number of available GPU devices is: %u\n", numGPUDevices);
    }

    if(dev_num >= numGPUDevices){
       printf("Error: Selected GPU device num: %u exeed available GPU devices number %d,"
              " selected correct device or check GPUs by clinfo for more help.\n",
              dev_num, numGPUDevices);
       return -1;
    }


    char dev_name[512];
    err = clGetDeviceInfo (devices[dev_num],
            CL_DEVICE_NAME ,
            sizeof(dev_name),
            dev_name,
            NULL);

    if (CL_SUCCESS != err){
                printf("Error[%d]: Failed to get platform %d device %d name info ...(%s)\n",
                       err, platform_num, dev_num, getclErrString(err));
                Cleanup_OpenCL();
                return -1;
    }
    else{
        printf("Deviece name: %s\n", dev_name);
    }

    cl_ulong max_dev_mem_alloc = 0;
    err = clGetDeviceInfo (devices[dev_num],
            CL_DEVICE_MAX_MEM_ALLOC_SIZE ,
            sizeof(max_dev_mem_alloc),
            &max_dev_mem_alloc,
            NULL);

    if (CL_SUCCESS != err){
                printf("Error[%d]: Failed to get platform %d device %d max mem alloc info ...(%s)\n",
                       err, platform_num, dev_num, getclErrString(err));
                Cleanup_OpenCL();
                return -1;
    }
    else{
        printf("Device max alloc memory size: %d MB\n", max_dev_mem_alloc>>20);
        if(g_conflict_map_size > max_dev_mem_alloc){
           printf("Error: Platform %d device %d max allocated %d MB memory exceed limit. \n",
                        platform_num, dev_num, g_conflict_map_size>>20);
           Cleanup_OpenCL();
           return -1;
        }
    }

    cl_ulong max_dev_gbl_mem = 0;
    err = clGetDeviceInfo (devices[dev_num],
            CL_DEVICE_GLOBAL_MEM_SIZE ,
            sizeof(max_dev_gbl_mem),
            &max_dev_gbl_mem,
            NULL);

    if (CL_SUCCESS != err){
                printf("Error[%d]: Failed to get platform %d device %d max global mem info ...(%s)\n",
                       err, platform_num, dev_num, getclErrString(err));
                Cleanup_OpenCL();
                return -1;
    }
    else{
        printf("Device max global memory size: %d MB\n", max_dev_gbl_mem>>20);
    }




    // create the OpenCL context on a GPU
    cl_context_properties props[3];
    props[0] = (cl_context_properties)CL_CONTEXT_PLATFORM;  // indicates that next element is platform
    props[1] = (cl_context_properties)ocl_platform_id;  // platform is of type cl_platform_id
    props[2] = (cl_context_properties)0;   // last element must be 0

   // g_context = clCreateContextFromType(props, CL_DEVICE_TYPE_GPU, NULL, NULL, &err);
    g_context = clCreateContext(props, 1, &devices[dev_num], NULL, NULL, &err);
    if (err != CL_SUCCESS) {
        printf("ERROR[%d]: Failed to create context from selected platform: %d GPU device. (%s) \n",
               err, platform_num, getclErrString(err));
        return -1;
    }
    /*if (g_context == (cl_context)0)
        return -1;*/


    // get the list of CPU devices associated with context
    cl_device_id selected_devices[MAX_GPU_NUM];
    size_t cb;
    err = clGetContextInfo(g_context, CL_CONTEXT_DEVICES, 0, NULL, &cb);
    if (err != CL_SUCCESS) {
        printf("ERROR[%d]: Failed to get context device number from platform: %d GPU device. (%s) \n",
               err, platform_num, getclErrString(err));
        return -1;
    }

    err = clGetContextInfo(g_context, CL_CONTEXT_DEVICES, cb, selected_devices, NULL);
    if (err != CL_SUCCESS) {
        printf("ERROR[%d]: Failed to get context device info from platform: %d GPU device. (%s) \n",
               err, platform_num, getclErrString(err));
        return -1;
    }

    if( alignment )
    {
        err = clGetDeviceInfo (devices[dev_num],
            CL_DEVICE_MEM_BASE_ADDR_ALIGN ,
            sizeof(cl_uint),
            alignment,
            NULL);

        *alignment/=8; //in bytes
        printf("Platform %d GPU Device %d OpenCL data alignment: %d bytes.\n",
               platform_num, dev_num, *alignment);
    }

    char ext_info[512];
    err = clGetDeviceInfo (devices[dev_num],
            CL_DEVICE_EXTENSIONS ,
            sizeof(ext_info),
            ext_info,
            NULL);

    if (CL_SUCCESS != err){
                printf("Error[%d]: Failed to get platform %d device %d info ...(%s)\n",
                       err, platform_num, dev_num, getclErrString(err));
                Cleanup_OpenCL();
                return -1;
    }
    else{
            if(strstr(ext_info, "cl_khr_global_int32_base_atomics") == NULL){
                printf("Error: This device %d opencl version is too old, can not be supported.\n", dev_num);
                Cleanup_OpenCL();
                return -1;
            }

            if(g_algo == AUTO){
                printf("[Info] Auto detect and specify GPU algorithm ...  ");
                g_algo = GEN;
                if(strstr(ext_info, "cl_amd")){
                    if(g_dbg_flag){
                        printf("--------+++-------> AMD GPU detected.\n");
                    }
                    if(strstr(dev_name, "Hawaii") ||
                       strstr(dev_name, "Malta") ||
                       strstr(dev_name, "Tahiti") ||
                       strstr(dev_name, "Pitcairn") ||
                       /*strstr(dev_name, "Bonaire ") || // fix here, 7790 not suitable GeekJ*/
                       strstr(dev_name, "Verde")){
                        g_algo = GEEKJ;
                        printf("AMD Southern Islands 28nm GCN GPU  detected: "
                               "Selected algorithm '%s'\n", gpu_algo_names[g_algo] );

                    }
                    else{
                        g_algo = KISS;
                        printf("AMD classic >= 40 nm GPU detected: "
                               "Selected algorithm '%s'\n", gpu_algo_names[g_algo] );
                        printf("[Info] Please compare with algorithm "
                               "'GeekJ' for the best perfomance!!\n" );
                    }
                }

                if(strstr(ext_info, "cl_nv")){
                    g_algo = GEN;
                    //printf("Error: A device %d is not supported.\n", dev_num);
                    //Cleanup_OpenCL();
                    g_nv_GPU = true;
                    if(g_dbg_flag){
                        printf("--------___--------> Nvidia GPU detected.\n");
                    }
                    printf("Nvidia GPU detected: "
                               "Selected algorithm '%s'\n", gpu_algo_names[g_algo] );

                }

                if(strstr(ext_info, "intel")){
                    g_algo = GEN;
                    //printf("Error: A device %d is not supported.\n", dev_num);
                    //Cleanup_OpenCL();
                    if(g_dbg_flag){
                        printf("--------:)--------> Intel GPU detected.\n");
                    }
                    printf("Intel GPU detected: "
                               "Selected algorithm '%s'\n", gpu_algo_names[g_algo] );

                }
            }else{
                printf("[Waring] Custom GPU algorithm selected '%s', maybe unstable!\n", gpu_algo_names[g_algo] );

            }
    }


    g_cmd_queue = clCreateCommandQueue(g_context, devices[dev_num], 0, &err);
    if( CL_SUCCESS != err){
        printf("Error[%d]: Failed to create CommandQueue on platform %d device %d.(%s)\n",
                err, platform_num, dev_num, getclErrString(err));
        Cleanup_OpenCL();
        return -1;
    }


    char *sources = NULL;


    bool ret = false; //load bin

    //TODO: Add bin format support here
    //snprintf(binfilename, sizeof(binfilename), "mom_p%d_d%d.bin", platform_num, dev_num );

    if(!ret){
        //printf("[Info] Can not find ocl binary file.\n");
        //exit(9);
        //printf("[Info] Can not load ocl binary file for a faster starting, load source instead.\n");

        char *sources = NULL;

        sources = ReadSources(program_source);
        if( NULL == sources ){
            printf("ERROR: Failed to read sources into memory :-( ...\n");
            Cleanup_OpenCL();
            return -1;
        }

        printf("[Waiting tip] Please wait for building ocl kernel source code, maybe 30 seconds more...\n");
        //printf("             Building result will be cached in binary file: %s\n", binfilename);

        g_program = clCreateProgramWithSource(g_context, 1, (const char**)&sources, NULL, &err);
        if (CL_SUCCESS != err)
        {
            printf("ERROR[%d]: Failed to create Program with source...\n", err);
            Cleanup_OpenCL();
            free(sources);
            return -1;
        }

        char CompilerOptions[1024];
        sprintf(CompilerOptions, " -D LOOK_UP_MASK=%d "
                " -D BITMAP_INDEX_TYPE=uint64_t -D BITMAP_SIZE=%d ",
                 (g_conflict_map_size-1)>>2, g_conflict_map_size);

        switch(g_algo){
                case GEEKJ: /*AMD GCN optimization here*/
                    if(g_amd_GPU){
                       char CompilerOptions2[512];
                       sprintf(CompilerOptions2,
                               " -D AMD_OPTIM ");
                       strncat(CompilerOptions, CompilerOptions2, sizeof(CompilerOptions));
                    }
                    break;
                case KISS: /*AMD classic optimization*/
                     if(g_amd_GPU){
                       char CompilerOptions2[512];
                       sprintf(CompilerOptions2,
                               " -D AMD_OPTIM2 ");
                       strncat(CompilerOptions, CompilerOptions2, sizeof(CompilerOptions));
                    }
                    break;
                default:
                    break;
            }


            if(g_nv_GPU){
               strncat(CompilerOptions, " -D NVPU ", sizeof(CompilerOptions));
            }

#ifdef OCL_DBG
            if(g_dbg_flag){
                strncat(CompilerOptions, " -D DEBUG_MODE ", sizeof(CompilerOptions));
                printf("CompilerOptions: %s\n", CompilerOptions);
            }
#endif

            err = clBuildProgram(g_program, 1, &devices[dev_num], CompilerOptions, NULL, NULL);
            if (err != CL_SUCCESS){
                printf("ERROR[%d]: Failed to build program...\n", err);
                BuildFailLog(g_program, devices[dev_num]);
                Cleanup_OpenCL();
                free(sources);
                return -1;
            }

        //存储编译好的kernel文件
        char **binaries = (char **)malloc( sizeof(char *) * 1 ); //只有一个设备
        size_t *binarySizes = (size_t*)malloc( sizeof(size_t) * 1 );

        err = clGetProgramInfo(g_program,
            CL_PROGRAM_BINARY_SIZES,
            sizeof(size_t) * 1,
            binarySizes, NULL);

        if (CL_SUCCESS != err){
            printf("WARNING[%d]: Failed to get program bin size ...(%s)\n",
                   err, getclErrString(err));
        }
        else{
            binaries[0] = (char *)malloc( sizeof(unsigned char) * binarySizes[0]);
            err = clGetProgramInfo(g_program,
                CL_PROGRAM_BINARIES,
                sizeof(char *) * 1,
                binaries,
                NULL);

            if (CL_SUCCESS != err){
                printf("WARNING[%d]: Failed to get program binaray ...(%s)\n",
                       err, getclErrString(err));
            }
            else{
                //kernelFile.writeBinaryToFile(binfilename, binaries[0], binarySizes[0]);
            }
        }
    }


    g_birthday_kernel = clCreateKernel(g_program, "birthdayPhase1", NULL);
    if (g_birthday_kernel == (cl_kernel)0)
    {
        printf("ERROR: Failed to create kernel momentum ...\n");
        Cleanup_OpenCL();
        free(sources);
        return -1;
    }

    g_match_kernel = clCreateKernel(g_program, "birthdayPhase2", NULL);
    if (g_match_kernel == (cl_kernel)0)
    {
        printf("ERROR: Failed to create kernel match...\n");
        Cleanup_OpenCL();
        free(sources);
        return -1;
    }

    free(sources);

    return 0; // success...
}

const char* getclErrString(cl_int errcode)
{
    switch (errcode) {
        case CL_SUCCESS:                            return "Success!";
        case CL_DEVICE_NOT_FOUND:                   return "Device not found.";
        case CL_DEVICE_NOT_AVAILABLE:               return "Device not available";
        case CL_COMPILER_NOT_AVAILABLE:             return "Compiler not available";
        case CL_MEM_OBJECT_ALLOCATION_FAILURE:      return "Memory object allocation failure";
        case CL_OUT_OF_RESOURCES:                   return "Out of resources";
        case CL_OUT_OF_HOST_MEMORY:                 return "Out of host memory";
        case CL_PROFILING_INFO_NOT_AVAILABLE:       return "Profiling information not available";
        case CL_MEM_COPY_OVERLAP:                   return "Memory copy overlap";
        case CL_IMAGE_FORMAT_MISMATCH:              return "Image format mismatch";
        case CL_IMAGE_FORMAT_NOT_SUPPORTED:         return "Image format not supported";
        case CL_BUILD_PROGRAM_FAILURE:              return "Program build failure";
        case CL_MAP_FAILURE:                        return "Map failure";
        case CL_INVALID_VALUE:                      return "Invalid value";
        case CL_INVALID_DEVICE_TYPE:                return "Invalid device type";
        case CL_INVALID_PLATFORM:                   return "Invalid platform";
        case CL_INVALID_DEVICE:                     return "Invalid device";
        case CL_INVALID_CONTEXT:                    return "Invalid context";
        case CL_INVALID_QUEUE_PROPERTIES:           return "Invalid queue properties";
        case CL_INVALID_COMMAND_QUEUE:              return "Invalid command queue";
        case CL_INVALID_HOST_PTR:                   return "Invalid host pointer";
        case CL_INVALID_MEM_OBJECT:                 return "Invalid memory object";
        case CL_INVALID_IMAGE_FORMAT_DESCRIPTOR:    return "Invalid image format descriptor";
        case CL_INVALID_IMAGE_SIZE:                 return "Invalid image size";
        case CL_INVALID_SAMPLER:                    return "Invalid sampler";
        case CL_INVALID_BINARY:                     return "Invalid binary";
        case CL_INVALID_BUILD_OPTIONS:              return "Invalid build options";
        case CL_INVALID_PROGRAM:                    return "Invalid program";
        case CL_INVALID_PROGRAM_EXECUTABLE:         return "Invalid program executable";
        case CL_INVALID_KERNEL_NAME:                return "Invalid kernel name";
        case CL_INVALID_KERNEL_DEFINITION:          return "Invalid kernel definition";
        case CL_INVALID_KERNEL:                     return "Invalid kernel";
        case CL_INVALID_ARG_INDEX:                  return "Invalid argument index";
        case CL_INVALID_ARG_VALUE:                  return "Invalid argument value";
        case CL_INVALID_ARG_SIZE:                   return "Invalid argument size";
        case CL_INVALID_KERNEL_ARGS:                return "Invalid kernel arguments";
        case CL_INVALID_WORK_DIMENSION:             return "Invalid work dimension";
        case CL_INVALID_WORK_GROUP_SIZE:            return "Invalid work group size";
        case CL_INVALID_WORK_ITEM_SIZE:             return "Invalid work item size";
        case CL_INVALID_GLOBAL_OFFSET:              return "Invalid global offset";
        case CL_INVALID_EVENT_WAIT_LIST:            return "Invalid event wait list";
        case CL_INVALID_EVENT:                      return "Invalid event";
        case CL_INVALID_OPERATION:                  return "Invalid operation";
        case CL_INVALID_GL_OBJECT:                  return "Invalid OpenGL object";
        case CL_INVALID_BUFFER_SIZE:                return "Invalid buffer size";
        case CL_INVALID_MIP_LEVEL:                  return "Invalid mip-map level";
        default: return "Unknown";
    }
}

bool ExecuteReadyKernel(unsigned int map_size)
{
    cl_int err = CL_SUCCESS;
    cl_uint4 pattern;
    memset(&pattern, 0, sizeof(cl_uint4));

    QueryPerformanceCounter(&g_PerformanceCountNDRangeStart);

    if(g_dbg_flag){
        puts("\nCall cl 1.2 clEnqueueFillBuffer ready arg 1 ...\n");
    }

    err = clEnqueueFillBuffer(g_cmd_queue, g_inputBuffer, &pattern, sizeof(cl_uint4), 0,
                        map_size, 0, NULL, NULL);
    if (err != CL_SUCCESS) {
        printf("ERROR[%d]: Failed to fill input buffer ready data size %d MBytes. (%s) \n",
               err, map_size>>20, getclErrString(err));
        return false;
    }

    if(g_dbg_flag){
        puts("\nCall cl 1.2 clEnqueueFillBuffer redy arg 2 ...\n");
    }
    err = clEnqueueFillBuffer(g_cmd_queue, g_matchBuffer, &pattern, sizeof(cl_uint4), 0,
                        MATCH_ARRAY_SIZE*sizeof(cl_uint), 0, NULL, NULL);

    if (err != CL_SUCCESS) {
        printf("ERROR[%d]: Failed to fill match buffer ready data size %d MBytes. (%s) \n",
               err, MATCH_ARRAY_SIZE*sizeof(cl_uint) >> 20, getclErrString(err));
        return false;
    }

    if(g_dbg_flag){
        puts("\nCall cl 1.2 clEnqueueFillBuffer redy arg 3 ...\n");
    }

    err = clEnqueueFillBuffer(g_cmd_queue, g_result, &pattern, sizeof(cl_uint4), 0,
                        RESULT_ARRAY_SIZE*sizeof(cl_uint), 0, NULL, NULL);

    if (err != CL_SUCCESS) {
        printf("ERROR[%d]: Failed to fill result buffer ready data. (%s) \n",
               err, getclErrString(err));
        return false;
    }

    QueryPerformanceCounter(&g_PerformanceCountNDRangeStop);
    return true;
}


static uint64 sha512_h0[8] =
            {0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL,
             0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
             0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
             0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL};

#define UNPACK32(x, str)                      \
{                                             \
    *((str) + 3) = (uint8) ((x)      );       \
    *((str) + 2) = (uint8) ((x) >>  8);       \
    *((str) + 1) = (uint8) ((x) >> 16);       \
    *((str) + 0) = (uint8) ((x) >> 24);       \
}

#define PACK32(str, x)                        \
{                                             \
    *(x) =   ((uint32) *((str) + 3)      )    \
           | ((uint32) *((str) + 2) <<  8)    \
           | ((uint32) *((str) + 1) << 16)    \
           | ((uint32) *((str) + 0) << 24);   \
}

#define UNPACK64(x, str)                      \
{                                             \
    *((str) + 7) = (uint8) ((x)      );       \
    *((str) + 6) = (uint8) ((x) >>  8);       \
    *((str) + 5) = (uint8) ((x) >> 16);       \
    *((str) + 4) = (uint8) ((x) >> 24);       \
    *((str) + 3) = (uint8) ((x) >> 32);       \
    *((str) + 2) = (uint8) ((x) >> 40);       \
    *((str) + 1) = (uint8) ((x) >> 48);       \
    *((str) + 0) = (uint8) ((x) >> 56);       \
}

#define PACK64(str, x)                        \
{                                             \
    *(x) =   ((uint64) *((str) + 7)      )    \
           | ((uint64) *((str) + 6) <<  8)    \
           | ((uint64) *((str) + 5) << 16)    \
           | ((uint64) *((str) + 4) << 24)    \
           | ((uint64) *((str) + 3) << 32)    \
           | ((uint64) *((str) + 2) << 40)    \
           | ((uint64) *((str) + 1) << 48)    \
           | ((uint64) *((str) + 0) << 56);   \
}


void sha512_midhash(uint64 *w, const unsigned char *message)
{
    //uint64 w[16];
    unsigned int block_nb;
    //unsigned int new_len, rem_len, tmp_len;
    //const unsigned char *shifted_message;

    sha512_ctx _ctx;
    sha512_ctx * ctx = &_ctx;

    int i;
    for (i = 0; i < 8; i++) {
        ctx->h[i] = sha512_h0[i];
    }

    ctx->len = 4;
    ctx->tot_len = 0;

    memset(&ctx->block[0], 0, 4); // fill 0 as birthday nonce
    memcpy(&ctx->block[ctx->len], message, 32);

    ctx->len += 32;

    block_nb = 1 + ((SHA512_BLOCK_SIZE - 17)
		< (ctx->len % SHA512_BLOCK_SIZE));

	unsigned int len_b = (ctx->tot_len + ctx->len) << 3;
	unsigned int pm_len = block_nb << 7;

	memset(ctx->block + ctx->len, 0, pm_len - ctx->len);
	memcpy((unsigned char*)w, ctx->block, 16*8);

	ctx->block[ctx->len] = 0x80;

	UNPACK32(len_b, ctx->block + pm_len - 4);

	for (int j = 0; j < 16; j++) {
        PACK64(&ctx->block[j << 3], &w[j]);
    }
}


extern "C" void  dumpBirthDayHash(const uint8* midHash, uint32 indexA);

bool ExecuteBirthdayKernel(cl_long** inputArray, /*cl_int arraySize,*/ const unsigned char * midhash,
                           const int nonce_offset)
{
    cl_int err = CL_SUCCESS;
    uint64_t inp[16];

    sha512_midhash(inp, midhash);

    //create OpenCL buffer using input array memory
    if(g_midhash==NULL){
        g_midhash = clCreateBuffer(g_context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                   MID_HASH_BUF_SIZE, (void*)inp, &err);

        if (CL_SUCCESS != err){
            printf("ERROR[%d]: Failed to create input mid hash buffer size(0x%x)Bytes, (%s)\n",
                   err, MID_HASH_BUF_SIZE, getclErrString(err));
            return false;
        }
    }


    if(g_offset==NULL){
        g_offset = clCreateBuffer(g_context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, sizeof(uint32),
                              (void*)&nonce_offset, &err);
    }
    if (CL_SUCCESS != err){
        printf("ERROR[%d]: Failed to create input nonce offset buffer size(0x%x)Bytes, (%s)\n",
               err, sizeof(uint32), getclErrString(err));
        return false;
    }

    err = clSetKernelArg(g_birthday_kernel, 0, sizeof(cl_mem), (void *) &g_midhash);
    if (err != CL_SUCCESS){
        printf("ERROR[%d]: Failed to set midhash kernel arguments. (%s)\n",
                err, getclErrString(err));
        return false;
    }

    err = clSetKernelArg(g_birthday_kernel, 1, sizeof(cl_mem), (void *) &g_inputBuffer);
    if (err != CL_SUCCESS)
    {
        printf("ERROR[%d]: Failed to set input buffer 1 kernel arguments. (%s) \n",
               err, getclErrString(err));
        return false;
    }


    err = clSetKernelArg(g_birthday_kernel, 2, sizeof(cl_mem), (void *) &g_matchBuffer);

    if (err != CL_SUCCESS)
    {
        printf("ERROR[%d]: Failed to set input match buffer kernel arguments. (%s) \n",
               err, getclErrString(err));
        return false;
    }

    err = clSetKernelArg(g_birthday_kernel, 3, sizeof(cl_mem), (void *) &g_offset);
    if (err != CL_SUCCESS){
        printf("ERROR: Failed to create kernel momentum. \n"
                /*err, getclErrString(err)*/);
        return false;
    }


    QueryPerformanceCounter(&g_PerformanceCountNDRangeStart);
    // set work-item dimensions
    size_t gsz = 1 <<24;
    size_t global_work_size[1] = {gsz};	//number of quad items in input array
    size_t ws = g_work_size;/*g_work_size*/
    size_t local_work_size[1]= {ws};					//valid WG sizes are 1:1024


    if(g_dbg_flag){
        printf("Run birthday kernel with gws: %d, lws: %d ...\n", gsz, ws);
    }

    // execute kernel
    err = clEnqueueNDRangeKernel(g_cmd_queue, g_birthday_kernel, 1,
                                             NULL, global_work_size,
                                             local_work_size, 0, NULL, NULL);
    if (CL_SUCCESS != err){
        printf("ERROR[%d]: Failed to execute birthday kernel (%s).\n", err, getclErrString(err));
        return false;
    }
    err = clFinish(g_cmd_queue);

    if (CL_SUCCESS != err){
        printf("ERROR[%d]: Failed to finish cmd queue (%s).\n", err, getclErrString(err));
        return false;
    }

    QueryPerformanceCounter(&g_PerformanceCountNDRangeStop);


    clEnqueueUnmapMemObject(g_cmd_queue, g_inputBuffer, *inputArray, 0, NULL, NULL);
    clReleaseMemObject(g_offset); g_offset = NULL;
    return true;
}


bool InitBirthdayBuffer(const unsigned int arraySize)
{
   //create OpenCL buffer
    cl_int err = CL_SUCCESS;
    if(g_inputBuffer == NULL){
        g_inputBuffer = clCreateBuffer(g_context,
                        CL_MEM_WRITE_ONLY, arraySize, NULL,&err);

        if (CL_SUCCESS != err){
            printf("ERROR[%d]: Failed to create input data Buffer, size: %u(0x%x) MBytes, (%s)\n",
                   err, arraySize>>20, arraySize>>20,
                   getclErrString(err));
            return false;
        }
        else{
        printf("[Info] Created input data Buffer, size: %u(0x%x) MBytes Ok.\n",
                   arraySize>>20, arraySize>>20);
        }
    }
    return true;
}

bool ExecuteMatchKernel(cl_uint** resultArray, const cl_uint pairs_found,
                        const unsigned int array_size)
{
    cl_int err = CL_SUCCESS;
    //cl_uint matchSize = pairs_found * 2 * sizeof(cl_uint);
    //create OpenCL buffer using input array memory

    err = clSetKernelArg(g_match_kernel, 0, sizeof(cl_mem), (void *) &g_midhash);
    if (err != CL_SUCCESS)
    {
        printf("ERROR[%d]: Failed to set midhash kernel arguments.(%s)\n",
               err, getclErrString(err));
        return false;
    }

    err = clSetKernelArg(g_match_kernel, 1, sizeof(cl_mem), (void *) &g_matchBuffer);
    //err |= clSetKernelArg(g_birthday_kernel, 1, sizeof(cl_uint4), (void *) &midstate0);
    //err |= clSetKernelArg(g_birthday_kernel, 2, sizeof(cl_mem), (void *) &g_offset);

    if (err != CL_SUCCESS){
        printf("ERROR[%d]: Failed to set input buffer kernel arguments.(%s)\n",
               err, getclErrString(err));
        return false;
    }

    err = clSetKernelArg(g_match_kernel, 2, sizeof(cl_mem), (void *) &g_result);
    if (err != CL_SUCCESS)
    {
        printf("ERROR[%d]: Failed to set result kernel arguments.(%s)\n",
               err, getclErrString(err));
        return false;
    }

    QueryPerformanceCounter(&g_PerformanceCountNDRangeStart);
    // set work-item dimensions
    size_t gsz =  array_size >> 1;//(g_vector_width/2);// >> 3;// << 10;
    size_t global_work_size[1] = {gsz};	//number of quad items in input array
    //size_t global_work_offset[1] = {0x100};
    size_t local_work_size[1]= { 64 };					//valid WG sizes are 1:1024

    // execute kernel
    err = clEnqueueNDRangeKernel(g_cmd_queue, g_match_kernel, 1,
                                             NULL, global_work_size,
                                             local_work_size, 0, NULL, NULL);
    if (CL_SUCCESS != err){
        printf("ERROR[%d]: Failed to execute match kernel (%s).\n", err, getclErrString(err));
        return false;
    }

    err = clFinish(g_cmd_queue);
    if (CL_SUCCESS != err){
        printf("ERROR[%d]: Failed to finish match cmd queue (%s).\n", err, getclErrString(err));
        return false;
    }

    QueryPerformanceCounter(&g_PerformanceCountNDRangeStop);

    *resultArray = (cl_uint *)clEnqueueMapBuffer(g_cmd_queue, g_result, true,
                                 CL_MAP_READ, 0, sizeof(cl_uint) * RESULT_ARRAY_SIZE,
                                 0, NULL, NULL, &err);

    if (CL_SUCCESS != err){
        printf("ERROR[%d]: Failed to map result buffer (%s).\n", err, getclErrString(err));
        return false;
    }

    err = clFinish(g_cmd_queue);
    if (CL_SUCCESS != err){
        printf("ERROR[%d]: Failed to finish cmd queue (%s).\n", err, getclErrString(err));
        return false;
    }

    return true;
}


void Usage()
{
    printf("Usage: ominer_kernel.exe [--help] -d device_enum [-s <VRam Size in MB>] [ -D ] [ -a (geekj|alpha|gen)] [-p platform]\n");
    printf("    -d GPU device enumration base 0 \n");
    exit(-1);
}


static unsigned int  totalCollisionCount = 0;

extern "C" void  dumpBirthDayHash(const uint8* midHash, uint32 indexA)
{
    uint32 hashData[9];
	uint8 *tempHash = (uint8 *)hashData;
	uint64 resultHash[8];
	memcpy(tempHash+4, midHash, 32);
	// get birthday A
	hashData[0] = indexA&~7;
	sha512_ctx c512;
	sha512_init(&c512);
	sha512_update(&c512, tempHash, 32+4);
	sha512_final(&c512, (unsigned char*)resultHash);
	printf("BirHash of 0x%08x:", indexA);
	for(unsigned int i =0 ; i< 8; i++){
            if ( (indexA&7) == i) putchar('['); else putchar(' ');
        printf("%016llx", resultHash[i]);
            if ( (indexA&7) == i) putchar(']'); else putchar(' ');
	}
    putchar('\n');
	//uint64 birthdayA = resultHash[indexA&7] >> (64ULL-SEARCH_SPACE_BITS);
}


extern "C" bool submit_validate(const unsigned char * block, bool verbose)
{
    uint8 midHash[32];
	sha256_ctx c256;
	sha256_init(&c256);
	sha256_update(&c256, (unsigned char*)block, 80);
	sha256_final(&c256, midHash);
	sha256_init(&c256);
	sha256_update(&c256, (unsigned char*)midHash, 32);
	sha256_final(&c256, midHash);

	unsigned int indexA = *(unsigned int*)(block + 80);
	unsigned int indexB = *(unsigned int*)(block + 84);

    uint32 hashData[9];
	uint8 * tempHash = (uint8 *)hashData;
	uint64 resultHash[8];
	memcpy(tempHash+4, midHash, 32);

	// get birthday A
	hashData[0] = indexA&~7;
	sha512_ctx c512;
	sha512_init(&c512);
	sha512_update(&c512, tempHash, 32+4);
	sha512_final(&c512, (unsigned char*)resultHash);
	uint64 birthdayA = resultHash[indexA&7] >> (64ULL-SEARCH_SPACE_BITS);
	//dump_hashes32((uint32*)resultHash, 16);

	// get birthday B
	hashData[0] = indexB&~7;
	sha512_init(&c512);
	sha512_update(&c512, tempHash, 32+4);
	sha512_final(&c512, (unsigned char*)resultHash);
	uint64 birthdayB = resultHash[indexB&7] >> (64ULL-SEARCH_SPACE_BITS);
    //dump_hashes32((uint32*)resultHash, 16);

	if( verbose ){
            printf("[Info]Validated  block:");
            for(int j=0; j<88; j++){
                   printf("%02x", block[j]);
            }
            printf("\n");
            printf("[Info] midhash:");
            for(int j=0; j<32; j++){
                   printf("%02x", midHash[j]);
            }
            printf("\n");
            printf("[Info]Validated result: A(0x%08x)->0x%016llx (M:0x%08x) B(0x%08x)->0x%016llx(M:0x%08x)\n",
                indexA,
                birthdayA,
                (uint32)((birthdayA>>18) & VAL_MASK),
                indexB,
                birthdayB,
                (uint32)((birthdayB>>18) & VAL_MASK));
                //*/
	}



	uint8 proofOfWorkHash[32];
	sha256_init(&c256);
	sha256_update(&c256, (unsigned char*)block, 80+8);
	sha256_final(&c256, proofOfWorkHash);
	sha256_init(&c256);
	sha256_update(&c256, (unsigned char*)proofOfWorkHash, 32);
	sha256_final(&c256, proofOfWorkHash);

	if(verbose){
            printf("[Info} POW hash: ");
            for(int i = 0; i< sizeof(proofOfWorkHash); i++)
                printf("%02x", proofOfWorkHash[i]);

            printf("\n");
	}

    if(birthdayA != birthdayB) {
       if(verbose){
          printf("[Error] Invalid collision.\n");
       }
       return false; // invalid collision
    }
	return true;
}

static bool conflict_validate(const char * block, const uint8* midHash,
                  uint32 indexA, uint32 indexB, uint64 *matchBirthDay)
{

#ifdef TEST_NO_VAL
    return false;
#endif
    uint32 hashData[9];
	uint8 * tempHash = (uint8 *)hashData;
	uint64 resultHash[8];
	memcpy(tempHash+4, midHash, 32);
	// get birthday A
	hashData[0] = indexA&~7;
	sha512_ctx c512;
	sha512_init(&c512);
	sha512_update(&c512, tempHash, 32+4);
	sha512_final(&c512, (unsigned char*)resultHash);
	uint64 birthdayA = resultHash[indexA&7] >> (64ULL-SEARCH_SPACE_BITS);
	//dump_hashes32((uint32*)resultHash, 16);

	// get birthday B
	hashData[0] = indexB&~7;
	sha512_init(&c512);
	sha512_update(&c512, tempHash, 32+4);
	sha512_final(&c512, (unsigned char*)resultHash);
	uint64 birthdayB = resultHash[indexB&7] >> (64ULL-SEARCH_SPACE_BITS);
    //dump_hashes32((uint32*)resultHash, 16);

	if( birthdayA != birthdayB )
	{
        if(g_dbg_flag){
               //*
               printf("[Info]Validated in midhash:");
            for(int j=0; j<32; j++){
                   printf("%02x", midHash[j]);
            }
            printf("\n");
            printf("[Info]Validated: A(0x%08x)->0x%016llx (M:0x%08x) B(0x%08x)->0x%016llx(M:0x%08x)\n",
                indexA,
                birthdayA,
                (uint32)((birthdayA>>18) & VAL_MASK),
                indexB,
                birthdayB,
                (uint32)((birthdayB>>18) & VAL_MASK));
                //*/
        }
		return false; // invalid collision
	}
	// birthday collision found
	*matchBirthDay = birthdayA;
	totalCollisionCount += 2;
	return true;

}


extern "C" int initGPUBuffer(cl_uint conflictSize);

int initGPUBuffer(cl_uint conflictSize)
{
    //create OpenCL buffer
    cl_int err = CL_SUCCESS;
    unsigned int bufSize = conflictSize;
    if(g_inputBuffer == NULL){
        g_inputBuffer = clCreateBuffer(g_context,
                        CL_MEM_WRITE_ONLY, bufSize, NULL,&err);

        if (CL_SUCCESS != err){
            printf("ERROR[%d]: Failed to create device data Buffer, size: %u(0x%x) MBytes, (%s)\n",
                   err, bufSize>>20, bufSize>>20,
                   getclErrString(err));
            return 1;
        }
        else{
        printf("[Info] Created device data buffer, size: %u(0x%x) MBytes Ok.\n",
                   bufSize>>20, bufSize>>20);
        }
    }

    bufSize = MATCH_ARRAY_SIZE * sizeof(cl_uint);
    if(g_matchBuffer == NULL){
        g_matchBuffer = clCreateBuffer(g_context,
                        CL_MEM_WRITE_ONLY, bufSize, NULL,&err);

        if (CL_SUCCESS != err){
            printf("ERROR[%d]: Failed to create device data Buffer, size: %u(0x%x) MBytes, (%s)\n",
                   err, bufSize>>20, bufSize>>20,
                   getclErrString(err));
            return 1;
        }
        else{
       /* printf("[Info] Created device data buffer, size: %u(0x%x) MBytes Ok.\n",
                   bufSize>>20, bufSize>>20);*/
        }
    }

    bufSize = RESULT_ARRAY_SIZE * sizeof(cl_uint);
    if(g_result == NULL){
        g_result = clCreateBuffer(g_context,
                        CL_MEM_READ_WRITE, bufSize, NULL,&err);

        if (CL_SUCCESS != err){
            printf("ERROR[%d]: Failed to create data Buffer, size: %u(0x%x) Bytes, (%s)\n",
                   err, bufSize, bufSize,
                   getclErrString(err));
            return 1;
        }
        else{
        /*printf("[Info] Created data buffer, size: %u(0x%x) Bytes Ok.\n",
                   bufSize, bufSize);*/
        }
    }
    return 0;
}

extern "C" int match_birthday_gpu_alg(unsigned int work_num,
                        cl_int map_size, const unsigned char* midhash,
                        unsigned int *nonce_array, unsigned int *found_num);



int match_birthday_gpu_alg(unsigned int work_num,
                        cl_int map_size, const unsigned char* midhash,
                        unsigned int *nonce_array, unsigned int *found_num)
{
    if(!ExecuteReadyKernel(map_size)){
        return 1;
    }


    QueryPerformanceFrequency(&g_PerfFrequency);


    if(work_num%g_stat_every_turns==0){
        printf("[G Stat] Counter rk time %f ms, ",
            1000.0f*(float)(g_PerformanceCountNDRangeStop.QuadPart -
                             g_PerformanceCountNDRangeStart.QuadPart)/(float)g_PerfFrequency.QuadPart);
    }

    cl_uint *result;

    if(!ExecuteBirthdayKernel((cl_long**)&result, midhash, 0)){
        return 1;
    }

    QueryPerformanceFrequency(&g_PerfFrequency);

    if(work_num%g_stat_every_turns==0){
        printf("k1 time %f ms, ",
            1000.0f*(float)(g_PerformanceCountNDRangeStop.QuadPart -
                             g_PerformanceCountNDRangeStart.QuadPart)/(float)g_PerfFrequency.QuadPart);
    }

    if(!ExecuteMatchKernel(&result, g_total_found, MATCH_ARRAY_SIZE)){
       return 1;
    }

    if(g_midhash){
          clReleaseMemObject(g_midhash);
          g_midhash = NULL;
    }

    QueryPerformanceFrequency(&g_PerfFrequency);
    if(work_num%g_stat_every_turns==0){
       printf(" k2 %f ms. ---->\n",
            1000.0f*(float)(g_PerformanceCountNDRangeStop.QuadPart -
                             g_PerformanceCountNDRangeStart.QuadPart)/(float)g_PerfFrequency.QuadPart);
    }

    cl_ulong tem;
    int found_cnt = result[1];
    int match_cnt = result[0];


    for(int k = 0; k<found_cnt; k++){
      if(result[2*k + 2]) {
            nonce_array[k*2]=result[2*k + 2];
            nonce_array[k*2+1]=result[2*k + 3];

        if(conflict_validate(NULL, midhash, nonce_array[k*2], nonce_array[k*2 + 1], &tem)){
           printf("Found conflict [%d]: %u(0x%08x) <-> %u(0x%08x) bir:%llx\n", found_cnt,
                nonce_array[k*2], nonce_array[k*2],
                nonce_array[k*2 + 1], nonce_array[k*2 + 1], tem);
         }
        }
    }

    *found_num = found_cnt*2;

    if(g_dbg_flag){ //(work_num%g_stat_every_turns==0){
        printf("Work %d found val/match :%d/%d\n", work_num, found_cnt, match_cnt);
    }

    cl_int err = clEnqueueUnmapMemObject(g_cmd_queue, g_result, result, 0, NULL, NULL);

    if (CL_SUCCESS != err){
        printf("ERROR[%d]: Failed to map result buffer (%s).\n", err, getclErrString(err));
        return false;
    }

    return 0;
}


void clean(int ret)
{
    printf("[Exiting]Releasing resources...\n");
    Cleanup_OpenCL();
    exit(ret);
}

#define SELF_TEST
#ifdef SELF_TEST

static unsigned int g_work_num = 0;
static int g_work_finished = 0;
static int g_work_started = 0;

static unsigned int g_test_arraySize = 0;

int main(int argc, _TCHAR* argv[])
{
    cl_uint dev_alignment = 128;
    //cl_bool sortAscending = true;

    cl_int arraySize = (1 << NONCE_BITS);

    g_conflict_map_size = 256 * (1<<20);
    int argn = 1;
    while (argn < argc)
    {
        if (strcmp(argv[argn], "--help") == 0 || strcmp(argv[argn], "-h") == 0)
        {
            Usage();
        }
        else if (strcmp(argv[argn], "-s") == 0)
        {
            if(++argn==argc)
                Usage();
            g_conflict_map_size = atoi(argv[argn]) << 20;
            argn++;
        }
        else if (strcmp(argv[argn], "-a") == 0)
        {
            switch(atoi(argv[argn+1])){
            case 0:
                g_algo = AUTO;
                break;
            case 1:
                g_algo = GEEKJ;
                break;
            case 2:
                g_algo = KISS;
                break;
            case 3:
                g_algo = GEN;
                break;
            }

            printf("Option algo selected: %s\n", gpu_algo_names[g_algo]);
            argn += 2;
            //sortAscending = false;
        }
        else if (strcmp(argv[argn], "-d") == 0)
        {
            g_device_num = atoi(argv[argn+1]);
            printf("Option device selected: %d\n", g_device_num);
            argn += 2;
            //sortAscending = false;
        }
        else if (strcmp(argv[argn], "-p") == 0)
        {
            g_platform_num = atoi(argv[argn+1]);
            printf("Option platform  selected: %d\n", g_platform_num);
            argn += 2;
            //sortAscending = false;
        }
        if (strcmp(argv[argn], "-D") == 0)
        {
            g_dbg_flag =true;
            g_stat_every_turns = 1;
            argn ++;
        }else if (strcmp(argv[argn], "-t") == 0)
        {
            g_run_turns = atoi(argv[argn+1]);
            printf("Option run turns: %d\n", g_run_turns);
            argn += 2;
        }else if (strcmp(argv[argn], "-G") == 0)
        {
            g_group_shift = atoi(argv[argn+1]);
            printf("Option group shift: %d\n", g_group_shift);
            argn += 2;
        }else if (strcmp(argv[argn], "-W") == 0)
        {
            g_work_size = atoi(argv[argn+1]);
            printf("Option worksize: %d\n", g_work_size);
            argn += 2;
        }
        else
        {
            argn++;
        }
    }

    if( argc < 2 )
    {
        printf("No command line arguments specified, using default values.\n");
    }

    g_group_size = 8;

    g_test_arraySize = arraySize;
    //jim test sha512 opencl
    printf("Initializing OpenCL runtime...\n");

    //initialize Open CL objects (context, queue, etc.)
    if( 0 != Setup_OpenCL("momentum_miner.cl", &dev_alignment, g_conflict_map_size,
                           g_algo, g_platform_num, g_device_num, 1) )
        return -1;

    //random input
    unsigned char block[80];
    unsigned char midhash[32];
    memset(block, 0, 80);

    g_work_num = 1;
    unsigned int  test_num = g_work_num;
    double totalConuterTime = 0;

    if(initGPUBuffer(g_conflict_map_size)){
        clean(1);
    }

    for(unsigned int i=test_num; i<test_num + g_run_turns; i++){
        block[0] = i;
        sha256_ctx c256;
        sha256_init(&c256);
        sha256_update(&c256, (unsigned char*)block, 80);
        sha256_final(&c256, midhash);
        sha256_init(&c256);
        sha256_update(&c256, (unsigned char*)midhash, 32);
        sha256_final(&c256, midhash);

        g_work_num = i;
        printf("test new mid hash: %02x%02x ...\n", midhash[0], midhash[1]);
        g_work_started = 1;
        while(!g_work_finished)Sleep(1);
         g_work_finished = 0;
         g_work_started = 0;

         printf("test new mid hash[%d]: %02x%02x %02x%02x ...  %02x%02x  %02x%02x\n",
           i, midhash[0], midhash[1], midhash[2], midhash[3],
              midhash[28], midhash[29], midhash[30], midhash[31]);


    QueryPerformanceCounter(&g_PerfTotalStart);

    unsigned int match_nonce[2*MAX_FOUND_IN_TURN];
    unsigned int match_num = 0;

    int ret = match_birthday_gpu_alg(i, g_conflict_map_size, midhash, match_nonce, &match_num);
    if(ret){
      printf("[Error]Failed to execute gpu kernel: %d\n", ret);
      exit(ret);
    }
    if(g_dbg_flag)
      printf("Return conflicts: %d\n", match_num);

    QueryPerformanceCounter(&g_PerfTotalStop);
    QueryPerformanceFrequency(&g_PerfFrequency);

    float this_turn_counter = 1000.0f*
        (float)((g_PerfTotalStop.QuadPart - g_PerfTotalStart.QuadPart)
                /(float)g_PerfFrequency.QuadPart);

    totalConuterTime += this_turn_counter;

    if(i%g_stat_every_turns==0){
        printf("[Perf]<---Work %u end. [conflicts:%u, meter:%.2f conflicts/min, runing:%.2f h].\n", i,
           totalCollisionCount, totalCollisionCount*60000.0/totalConuterTime,
           totalConuterTime/3600000.0f);
    }


   }

    clean(0);
    return 0;
}
#endif
