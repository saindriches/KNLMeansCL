/*
*    This file is part of KNLMeansCL,
*    Copyright(C) 2015-2016  Edoardo Brunetti.
*
*    KNLMeansCL is free software: you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    KNLMeansCL is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with KNLMeansCL. If not, see <http://www.gnu.org/licenses/>.
*
*    To speed up processing I use an algorithm proposed by B. Goossens,
*    H.Q. Luong, J. Aelterman, A. Pizurica,  and W. Philips, "A GPU-Accelerated
*    Real-Time NLMeans Algorithm for Denoising Color Video Sequences",
*    in Proc. ACIVS (2), 2010, pp.46-57.
*/

//////////////////////////////////////////
// Type Definition

#define nlmDistance                0x0
#define nlmHorizontal              0x1
#define nlmVertical                0x2
#define nlmAccumulation            0x3
#define nlmFinish                  0x4
#define nlmPack                    0x5
#define nlmUnpack                  0x6
#define NLM_KERNELS                0x7

#define memU1a                     0x0
#define memU1b                     0x1
#define memU1z                     0x2
#define memU2                      0x3
#define memU4a                     0x4
#define memU4b                     0x5
#define memU5                      0x6
#define NLM_MEMORY                 0x7

#define NLM_CLIP_EXTRA_FALSE      (1 << 0)
#define NLM_CLIP_EXTRA_TRUE       (1 << 1)
#define NLM_CLIP_TYPE_UNORM       (1 << 2)
#define NLM_CLIP_TYPE_UNSIGNED    (1 << 3)
#define NLM_CLIP_TYPE_STACKED     (1 << 4)
#define NLM_CLIP_REF_LUMA         (1 << 5)
#define NLM_CLIP_REF_CHROMA       (1 << 6)
#define NLM_CLIP_REF_YUV          (1 << 7)
#define NLM_CLIP_REF_RGB          (1 << 8)

#define NLM_WMODE_WELSCH           0x0
#define NLM_WMODE_BISQUARE1        0x1
#define NLM_WMODE_BISQUARE2        0x2
#define NLM_WMODE_BISQUARE8        0x3

#define HRZ_RESULT                   3
#define VRT_RESULT                   3

//////////////////////////////////////////
// Kernel Definition
static const char* kernel_source_code =
"                                                                                                                 \n" \
"#define NLM_NORM            ( 255.0f * 255.0f )                                                                  \n" \
"#define NLM_S_SIZE          ( (2 * NLM_S + 1) * (2 * NLM_S + 1) )                                                \n" \
"#define NLM_H2_INV_NORM     ( NLM_NORM / (NLM_H * NLM_H * NLM_S_SIZE) )                                          \n" \
"#define NLM_16BIT_MSB       ( 256.0f / (257.0f * 255.0f) )                                                       \n" \
"#define NLM_16BIT_LSB       (   1.0f / (257.0f * 255.0f) )                                                       \n" \
"#define CHECK_FLAG(flag)    ( (NLM_TCLIP & (flag)) == (flag) )                                                   \n" \
"                                                                                                                 \n" \
"__kernel __attribute__((reqd_work_group_size(DST_BLOCK_X, DST_BLOCK_Y, 1)))                                      \n" \
"void nlmDistance(__read_only image2d_array_t U1, __write_only image2d_array_t U4, const int t, const int4 q) {   \n" \
"                                                                                                                 \n" \
"   int x = get_global_id(0);                                                                                     \n" \
"   int y = get_global_id(1);                                                                                     \n" \
"   if (x >= VI_DIM_X || y >= VI_DIM_Y) return;                                                                   \n" \
"                                                                                                                 \n" \
"   const sampler_t smp = CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_NONE | CLK_FILTER_NEAREST;                    \n" \
"   int4 p     = (int4) (x, y, t, 0);                                                                             \n" \
"   int  x_pq = VI_DIM_X - abs_diff(x + q.x, VI_DIM_X - 1);                                                       \n" \
"   int  y_pq = VI_DIM_Y - abs_diff(y + q.y, VI_DIM_Y - 1);                                                       \n" \
"   int4 p_pq  = (int4) (x_pq, y_pq, t + q.z, 0);                                                                 \n" \
"                                                                                                                 \n" \
"   if (CHECK_FLAG(NLM_CLIP_REF_LUMA)) {                                                                          \n" \
"                                                                                                                 \n" \
"       float  u1    = read_imagef(U1, smp, p   ).x;                                                              \n" \
"       float  u1_pq = read_imagef(U1, smp, p_pq).x;                                                              \n" \
"       float  val   = 3.0f * (u1 - u1_pq) * (u1 - u1_pq);                                                        \n" \
"       write_imagef(U4, p, (float4) (val, 0.0f, 0.0f, 0.0f));                                                    \n" \
"                                                                                                                 \n" \
"   } else if (CHECK_FLAG(NLM_CLIP_REF_CHROMA)) {                                                                 \n" \
"                                                                                                                 \n" \
"       float2 u1    = read_imagef(U1, smp, p   ).xy;                                                             \n" \
"       float2 u1_pq = read_imagef(U1, smp, p_pq).xy;                                                             \n" \
"       float  dst_u = (u1.x - u1_pq.x) * (u1.x - u1_pq.x);                                                       \n" \
"       float  dst_v = (u1.y - u1_pq.y) * (u1.y - u1_pq.y);                                                       \n" \
"       float  val   = 1.5f * (dst_u + dst_v);                                                                    \n" \
"       write_imagef(U4, p, (float4) (val, 0.0f, 0.0f, 0.0f));                                                    \n" \
"                                                                                                                 \n" \
"   } else if (CHECK_FLAG(NLM_CLIP_REF_YUV)) {                                                                    \n" \
"                                                                                                                 \n" \
"       float3 u1    = read_imagef(U1, smp, p   ).xyz;                                                            \n" \
"       float3 u1_pq = read_imagef(U1, smp, p_pq).xyz;                                                            \n" \
"       float  dst_y = (u1.x - u1_pq.x) * (u1.x - u1_pq.x);                                                       \n" \
"       float  dst_u = (u1.y - u1_pq.y) * (u1.y - u1_pq.y);                                                       \n" \
"       float  dst_v = (u1.z - u1_pq.z) * (u1.z - u1_pq.z);                                                       \n" \
"       float  val   = dst_y + dst_u + dst_v;                                                                     \n" \
"       write_imagef(U4, p, (float4) (val, 0.0f, 0.0f, 0.0f));                                                    \n" \
"                                                                                                                 \n" \
"   } else if (CHECK_FLAG(NLM_CLIP_REF_RGB)) {                                                                    \n" \
"                                                                                                                 \n" \
"       float3 u1    = read_imagef(U1, smp, p   ).xyz;                                                            \n" \
"       float3 u1_pq = read_imagef(U1, smp, p_pq).xyz;                                                            \n" \
"       float  m_red = native_divide(u1.x + u1_pq.x, 6.0f);                                                       \n" \
"       float  dst_r = (2.0f/3.0f + m_red) * (u1.x - u1_pq.x) * (u1.x - u1_pq.x);                                 \n" \
"       float  dst_g = (4.0f/3.0f        ) * (u1.y - u1_pq.y) * (u1.y - u1_pq.y);                                 \n" \
"       float  dst_b = (     1.0f - m_red) * (u1.z - u1_pq.z) * (u1.z - u1_pq.z);                                 \n" \
"       float  val   = dst_r + dst_g + dst_b;                                                                     \n" \
"       write_imagef(U4, p, (float4) (val, 0.0f, 0.0f, 0.0f));                                                    \n" \
"                                                                                                                 \n" \
"   }                                                                                                             \n" \
"}                                                                                                                \n" \
"                                                                                                                 \n" \
"__kernel __attribute__((reqd_work_group_size(HRZ_BLOCK_X, HRZ_BLOCK_Y, 1)))                                      \n" \
"void nlmHorizontal(__read_only image2d_array_t U4_in, __write_only image2d_array_t U4_out, const int t) {        \n" \
"                                                                                                                 \n" \
"   __local float buffer[HRZ_BLOCK_Y][(HRZ_RESULT + 2) * HRZ_BLOCK_X];                                            \n" \
"   int x = (get_group_id(0) * HRZ_RESULT - 1) * HRZ_BLOCK_X + get_local_id(0);                                   \n" \
"   int y = get_group_id(1) * HRZ_BLOCK_Y + get_local_id(1);                                                      \n" \
"                                                                                                                 \n" \
"   const sampler_t smp = CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_CLAMP | CLK_FILTER_NEAREST;                   \n" \
"                                                                                                                 \n" \
"   for (int i = 1; i < 1 + HRZ_RESULT; i++)                                                                      \n" \
"       buffer[get_local_id(1)][get_local_id(0) + i * HRZ_BLOCK_X] =                                              \n" \
"           read_imagef(U4_in, smp, (int4) (x + i * HRZ_BLOCK_X, y, t, 0)).x;                                     \n" \
"                                                                                                                 \n" \
"   buffer[get_local_id(1)][get_local_id(0)] =                                                                    \n" \
"       read_imagef(U4_in, smp, (int4) (x, y, t, 0)).x;                                                           \n" \
"                                                                                                                 \n" \
"   buffer[get_local_id(1)][get_local_id(0) + (1 + HRZ_RESULT) * HRZ_BLOCK_X] =                                   \n" \
"       read_imagef(U4_in, smp, (int4) (x + (1 + HRZ_RESULT) * HRZ_BLOCK_X, y, t, 0)).x;                          \n" \
"                                                                                                                 \n" \
"   barrier(CLK_LOCAL_MEM_FENCE);                                                                                 \n" \
"                                                                                                                 \n" \
"   for (int i = 1; i < 1 + HRZ_RESULT; i++) {                                                                    \n" \
"       if ((x + i * HRZ_BLOCK_X) >= VI_DIM_X || y >= VI_DIM_Y) return;                                           \n" \
"       float sum = 0.0f;                                                                                         \n" \
"       for (int j = -NLM_S; j <= NLM_S; j++)                                                                     \n" \
"           sum += buffer[get_local_id(1)][get_local_id(0) + i * HRZ_BLOCK_X + j];                                \n" \
"                                                                                                                 \n" \
"       write_imagef(U4_out, (int4) (x + i * HRZ_BLOCK_X, y, t, 0), (float4) (sum, 0.0f, 0.0f, 0.0f));            \n" \
"   }                                                                                                             \n" \
"}                                                                                                                \n" \
"                                                                                                                 \n" \
"__kernel __attribute__((reqd_work_group_size(VRT_BLOCK_X, VRT_BLOCK_Y, 1)))                                      \n" \
"void nlmVertical(__read_only image2d_array_t U4_in, __write_only image2d_array_t U4_out, const int t) {          \n" \
"                                                                                                                 \n" \
"   __local float buffer[VRT_BLOCK_X][(VRT_RESULT + 2) * VRT_BLOCK_Y + 1];                                        \n" \
"   int x = get_group_id(0) * VRT_BLOCK_X + get_local_id(0);                                                      \n" \
"   int y = (get_group_id(1) * VRT_RESULT - 1) * VRT_BLOCK_Y + get_local_id(1);                                   \n" \
"                                                                                                                 \n" \
"   const sampler_t smp = CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_CLAMP | CLK_FILTER_NEAREST;                   \n" \
"                                                                                                                 \n" \
"   for (int i = 1; i < 1 + VRT_RESULT; i++)                                                                      \n" \
"       buffer[get_local_id(0)][get_local_id(1) + i * VRT_BLOCK_Y] =                                              \n" \
"           read_imagef(U4_in, smp, (int4) (x, y + i * VRT_BLOCK_Y, t, 0)).x;                                     \n" \
"                                                                                                                 \n" \
"   buffer[get_local_id(0)][get_local_id(1)] =                                                                    \n" \
"       read_imagef(U4_in, smp, (int4) (x, y, t, 0)).x;                                                           \n" \
"                                                                                                                 \n" \
"   buffer[get_local_id(0)][get_local_id(1) + (1 + VRT_RESULT) * VRT_BLOCK_Y] =                                   \n" \
"       read_imagef(U4_in, smp, (int4) (x, y + (1 + VRT_RESULT) * VRT_BLOCK_Y, t, 0)).x;                          \n" \
"                                                                                                                 \n" \
"   barrier(CLK_LOCAL_MEM_FENCE);                                                                                 \n" \
"                                                                                                                 \n" \
"   for (int i = 1; i < 1 + VRT_RESULT; i++) {                                                                    \n" \
"       if (x >= VI_DIM_X || (y + i * VRT_BLOCK_Y) >= VI_DIM_Y) return;                                           \n" \
"       float sum = 0.0f;                                                                                         \n" \
"       for (int j = -NLM_S; j <= NLM_S; j++)                                                                     \n" \
"           sum += buffer[get_local_id(0)][get_local_id(1) + i * VRT_BLOCK_Y + j];                                \n" \
"                                                                                                                 \n" \
"       float val = 0.0f;                                                                                         \n" \
"       if (NLM_WMODE == NLM_WMODE_WELSCH) {                                                                      \n" \
"           val = native_exp(- sum * NLM_H2_INV_NORM);                                                            \n" \
"       } else if (NLM_WMODE == NLM_WMODE_BISQUARE1) {                                                            \n" \
"           val = fdim(1.0f, sum * NLM_H2_INV_NORM);                                                              \n" \
"       } else if (NLM_WMODE == NLM_WMODE_BISQUARE2) {                                                            \n" \
"           val = pown(fdim(1.0f, sum * NLM_H2_INV_NORM), 2);                                                     \n" \
"       } else if (NLM_WMODE == NLM_WMODE_BISQUARE8) {                                                            \n" \
"           val = pown(fdim(1.0f, sum * NLM_H2_INV_NORM), 8);                                                     \n" \
"       }                                                                                                         \n" \
"       write_imagef(U4_out, (int4) (x, y + i * VRT_BLOCK_Y, t, 0), (float4) (val, 0.0f, 0.0f, 0.0f));            \n" \
"   }                                                                                                             \n" \
"}                                                                                                                \n" \
"                                                                                                                 \n" \
"__kernel                                                                                                         \n" \
"void nlmAccumulation(__read_only image2d_array_t U1, __global float* U2, __read_only image2d_array_t U4,         \n" \
"__global float* U5, const int4 q) {                                                                              \n" \
"                                                                                                                 \n" \
"   int x = get_global_id(0);                                                                                     \n" \
"   int y = get_global_id(1);                                                                                     \n" \
"   if (x >= VI_DIM_X || y >= VI_DIM_Y) return;                                                                   \n" \
"                                                                                                                 \n" \
"   const sampler_t smp = CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_CLAMP | CLK_FILTER_NEAREST;                   \n" \
"   int4 p = (int4) (x, y, NLM_D, 0);                                                                             \n" \
"   int2 s = (int2) (x, y);                                                                                       \n" \
"   int gidx = mad24(y, VI_DIM_X, x);                                                                             \n" \
"                                                                                                                 \n" \
"   float u4    = read_imagef(U4, smp, p    ).x;                                                                  \n" \
"   float u4_mq = read_imagef(U4, smp, p - q).x;                                                                  \n" \
"   U5[gidx]    = fmax(U5[gidx], fmax(u4, u4_mq));                                                                \n" \
"                                                                                                                 \n" \
"   if (CHECK_FLAG(NLM_CLIP_REF_LUMA)) {                                                                          \n" \
"                                                                                                                 \n" \
"       float  u1_pq = read_imagef(U1, smp, p + q).x;                                                             \n" \
"       float  u1_mq = read_imagef(U1, smp, p - q).x;                                                             \n" \
"       int    x_idx = mad24(y, 2 * VI_DIM_X, mul24(2, x));                                                       \n" \
"       int    w_idx = x_idx + 1;                                                                                 \n" \
"       U2[x_idx]   += (u4 * u1_pq) + (u4_mq * u1_mq);                                                            \n" \
"       U2[w_idx]   += (u4 + u4_mq);                                                                              \n" \
"                                                                                                                 \n" \
"   } else if (CHECK_FLAG(NLM_CLIP_REF_CHROMA)) {                                                                 \n" \
"                                                                                                                 \n" \
"       float2 u1_pq = read_imagef(U1, smp, p + q).xy;                                                            \n" \
"       float2 u1_mq = read_imagef(U1, smp, p - q).xy;                                                            \n" \
"       int    x_idx = mad24(y, 4 * VI_DIM_X, mul24(4, x));                                                       \n" \
"       int    y_idx = x_idx + 1;                                                                                 \n" \
"       int    w_idx = x_idx + 2;                                                                                 \n" \
"       U2[x_idx]   += (u4 * u1_pq.x) + (u4_mq * u1_mq.x);                                                        \n" \
"       U2[y_idx]   += (u4 * u1_pq.y) + (u4_mq * u1_mq.y);                                                        \n" \
"       U2[w_idx]   += (u4 + u4_mq);                                                                              \n" \
"                                                                                                                 \n" \
"   } else if (CHECK_FLAG(NLM_CLIP_REF_YUV)) {                                                                    \n" \
"                                                                                                                 \n" \
"       float3 u1_pq = read_imagef(U1, smp, p + q).xyz;                                                           \n" \
"       float3 u1_mq = read_imagef(U1, smp, p - q).xyz;                                                           \n" \
"       int    x_idx = mad24(y, 4 * VI_DIM_X,  mul24(4, x));                                                      \n" \
"       int    y_idx = x_idx + 1;                                                                                 \n" \
"       int    z_idx = x_idx + 2;                                                                                 \n" \
"       int    w_idx = x_idx + 3;                                                                                 \n" \
"       U2[x_idx]   += (u4 * u1_pq.x) + (u4_mq * u1_mq.x);                                                        \n" \
"       U2[y_idx]   += (u4 * u1_pq.y) + (u4_mq * u1_mq.y);                                                        \n" \
"       U2[z_idx]   += (u4 * u1_pq.z) + (u4_mq * u1_mq.z);                                                        \n" \
"       U2[w_idx]   += (u4 + u4_mq);                                                                              \n" \
"                                                                                                                 \n" \
"   } else if (CHECK_FLAG(NLM_CLIP_REF_RGB)) {                                                                    \n" \
"                                                                                                                 \n" \
"       float3 u1_pq = read_imagef(U1, smp, p + q).xyz;                                                           \n" \
"       float3 u1_mq = read_imagef(U1, smp, p - q).xyz;                                                           \n" \
"       int    x_idx = mad24(y, 4 * VI_DIM_X, mul24(4, x));                                                       \n" \
"       int    y_idx = x_idx + 1;                                                                                 \n" \
"       int    z_idx = x_idx + 2;                                                                                 \n" \
"       int    w_idx = x_idx + 3;                                                                                 \n" \
"       U2[x_idx]   += (u4 * u1_pq.x) + (u4_mq * u1_mq.x);                                                        \n" \
"       U2[y_idx]   += (u4 * u1_pq.y) + (u4_mq * u1_mq.y);                                                        \n" \
"       U2[z_idx]   += (u4 * u1_pq.z) + (u4_mq * u1_mq.z);                                                        \n" \
"       U2[w_idx]   += (u4 + u4_mq);                                                                              \n" \
"                                                                                                                 \n" \
"   }                                                                                                             \n" \
"}                                                                                                                \n" \
"                                                                                                                 \n" \
"__kernel                                                                                                         \n" \
"void nlmFinish(__read_only image2d_array_t U1_in, __write_only image2d_t U1_out, __global void* U2,              \n" \
"__global float* U5) {                                                                                            \n" \
"                                                                                                                 \n" \
"   int x = get_global_id(0);                                                                                     \n" \
"   int y = get_global_id(1);                                                                                     \n" \
"   if (x >= VI_DIM_X || y >= VI_DIM_Y) return;                                                                   \n" \
"                                                                                                                 \n" \
"   const sampler_t smp = CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_NONE | CLK_FILTER_NEAREST;                    \n" \
"   int4 p = (int4) (x, y, NLM_D, 0);                                                                             \n" \
"   int2 s = (int2) (x, y);                                                                                       \n" \
"   int gidx = mad24(y, VI_DIM_X, x);                                                                             \n" \
"   float wM = NLM_WREF * U5[gidx];                                                                               \n" \
"                                                                                                                 \n" \
"   if (CHECK_FLAG(NLM_CLIP_REF_LUMA)) {                                                                          \n" \
"                                                                                                                 \n" \
"       __global float2* U2c = (__global float2*) U2;                                                             \n" \
"       float  u1    = read_imagef(U1_in, smp, p).x;                                                              \n" \
"       float  num   = U2c[gidx].x + wM * u1;                                                                     \n" \
"       float  den   = U2c[gidx].y + wM;                                                                          \n" \
"       float  val   = native_divide(num, den);                                                                   \n" \
"       write_imagef(U1_out, s, (float4) (val, 0.0f, 0.0f, 0.0f));                                                \n" \
"                                                                                                                 \n" \
"   } else if (CHECK_FLAG(NLM_CLIP_REF_CHROMA)) {                                                                 \n" \
"                                                                                                                 \n" \
"       __global float4* U2c = (__global float4*) U2;                                                             \n" \
"       float2 u1    = read_imagef(U1_in, smp, p).xy;                                                             \n" \
"       float  num_u = U2c[gidx].x + wM * u1.x;                                                                   \n" \
"       float  num_v = U2c[gidx].y + wM * u1.y;                                                                   \n" \
"       float  den   = U2c[gidx].z + wM;                                                                          \n" \
"       float  val_u = native_divide(num_u, den);                                                                 \n" \
"       float  val_v = native_divide(num_v, den);                                                                 \n" \
"       write_imagef(U1_out, s,  (float4) (val_u, val_v, 0.0f, 0.0f));                                            \n" \
"                                                                                                                 \n" \
"   } else if (CHECK_FLAG(NLM_CLIP_REF_YUV)) {                                                                    \n" \
"                                                                                                                 \n" \
"       __global float4* U2c = (__global float4*) U2;                                                             \n" \
"       float3 u1    = read_imagef(U1_in, smp, p).xyz;                                                            \n" \
"       float  num_y = U2c[gidx].x + wM * u1.x;                                                                   \n" \
"       float  num_u = U2c[gidx].y + wM * u1.y;                                                                   \n" \
"       float  num_v = U2c[gidx].z + wM * u1.z;                                                                   \n" \
"       float  den   = U2c[gidx].w + wM;                                                                          \n" \
"       float  val_y = native_divide(num_y, den);                                                                 \n" \
"       float  val_u = native_divide(num_u, den);                                                                 \n" \
"       float  val_v = native_divide(num_v, den);                                                                 \n" \
"       write_imagef(U1_out, s,  (float4) (val_y, val_u, val_v, 0.0f));                                           \n" \
"                                                                                                                 \n" \
"   } else if (CHECK_FLAG(NLM_CLIP_REF_RGB)) {                                                                    \n" \
"                                                                                                                 \n" \
"       __global float4* U2c = (__global float4*) U2;                                                             \n" \
"       float3 u1    = read_imagef(U1_in, smp, p).xyz;                                                            \n" \
"       float  num_r = U2c[gidx].x + wM * u1.x;                                                                   \n" \
"       float  num_g = U2c[gidx].y + wM * u1.y;                                                                   \n" \
"       float  num_b = U2c[gidx].z + wM * u1.z;                                                                   \n" \
"       float  den   = U2c[gidx].w + wM;                                                                          \n" \
"       float  val_r = native_divide(num_r, den);                                                                 \n" \
"       float  val_g = native_divide(num_g, den);                                                                 \n" \
"       float  val_b = native_divide(num_b, den);                                                                 \n" \
"       write_imagef(U1_out, s,  (float4) (val_r, val_g, val_b, 0.0f));                                           \n" \
"                                                                                                                 \n" \
"   }                                                                                                             \n" \
"}                                                                                                                \n" \
"                                                                                                                 \n" \
"__kernel                                                                                                         \n" \
"void nlmPack(__read_only image2d_t R, __read_only image2d_t G, __read_only image2d_t B,                          \n" \
"__read_only image2d_t R_lsb, __read_only image2d_t G_lsb, __read_only image2d_t B_lsb,                           \n" \
"__write_only image2d_array_t U1, const int t) {                                                                  \n" \
"                                                                                                                 \n" \
"   int x = get_global_id(0);                                                                                     \n" \
"   int y = get_global_id(1);                                                                                     \n" \
"   if (x >= VI_DIM_X || y >= VI_DIM_Y) return;                                                                   \n" \
"                                                                                                                 \n" \
"   const sampler_t smp = CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_NONE | CLK_FILTER_NEAREST;                    \n" \
"   int4 p = (int4) (x, y, t, 0);                                                                                 \n" \
"   int2 s = (int2) (x, y);                                                                                       \n" \
"                                                                                                                 \n" \
"   if (CHECK_FLAG(NLM_CLIP_TYPE_STACKED | NLM_CLIP_REF_LUMA)) {                                                  \n" \
"       float y_msb = convert_float(read_imageui(R,     smp, s).x);                                               \n" \
"       float y_lsb = convert_float(read_imageui(R_lsb, smp, s).x);                                               \n" \
"       float y     = NLM_16BIT_MSB * y_msb + NLM_16BIT_LSB * y_lsb;                                              \n" \
"       write_imagef(U1, p, (float4) (y, 0.0f, 0.0f, 0.0f));                                                      \n" \
"   } else if (CHECK_FLAG(NLM_CLIP_TYPE_UNORM | NLM_CLIP_REF_CHROMA)) {                                           \n" \
"       float u     = read_imagef(R, smp, s).x;                                                                   \n" \
"       float v     = read_imagef(G, smp, s).x;                                                                   \n" \
"       write_imagef(U1, p, (float4) (u, v, 0.0f, 0.0f));                                                         \n" \
"   } else if (CHECK_FLAG(NLM_CLIP_TYPE_STACKED | NLM_CLIP_REF_CHROMA)) {                                         \n" \
"       float u_msb = convert_float(read_imageui(R,     smp, s).x);                                               \n" \
"       float v_msb = convert_float(read_imageui(G,     smp, s).x);                                               \n" \
"       float u_lsb = convert_float(read_imageui(R_lsb, smp, s).x);                                               \n" \
"       float v_lsb = convert_float(read_imageui(G_lsb, smp, s).x);                                               \n" \
"       float u     = NLM_16BIT_MSB * u_msb + NLM_16BIT_LSB * u_lsb;                                              \n" \
"       float v     = NLM_16BIT_MSB * v_msb + NLM_16BIT_LSB * v_lsb;                                              \n" \
"       write_imagef(U1, p, (float4) (u, v, 0.0f, 0.0f));                                                         \n" \
"   } else if (CHECK_FLAG(NLM_CLIP_TYPE_UNORM | NLM_CLIP_REF_YUV)) {                                              \n" \
"       float y     = read_imagef(R, smp, s).x;                                                                   \n" \
"       float u     = read_imagef(G, smp, s).x;                                                                   \n" \
"       float v     = read_imagef(B, smp, s).x;                                                                   \n" \
"       write_imagef(U1, p, (float4) (y, u, v, 0.0f));                                                            \n" \
"   } else if (CHECK_FLAG(NLM_CLIP_TYPE_UNSIGNED | NLM_CLIP_REF_YUV)) {                                           \n" \
"       float y     = native_divide(convert_float(read_imageui(R, smp, s).x), 1023.0f);                           \n" \
"       float u     = native_divide(convert_float(read_imageui(G, smp, s).x), 1023.0f);                           \n" \
"       float v     = native_divide(convert_float(read_imageui(B, smp, s).x), 1023.0f);                           \n" \
"       write_imagef(U1, p, (float4) (y, u, v, 0.0f));                                                            \n" \
"   } else if (CHECK_FLAG(NLM_CLIP_TYPE_STACKED | NLM_CLIP_REF_YUV)) {                                            \n" \
"       float y_msb = convert_float(read_imageui(R,     smp, s).x);                                               \n" \
"       float u_msb = convert_float(read_imageui(G,     smp, s).x);                                               \n" \
"       float v_msb = convert_float(read_imageui(B,     smp, s).x);                                               \n" \
"       float y_lsb = convert_float(read_imageui(R_lsb, smp, s).x);                                               \n" \
"       float u_lsb = convert_float(read_imageui(G_lsb, smp, s).x);                                               \n" \
"       float v_lsb = convert_float(read_imageui(B_lsb, smp, s).x);                                               \n" \
"       float y     = NLM_16BIT_MSB * y_msb + NLM_16BIT_LSB * y_lsb;                                              \n" \
"       float u     = NLM_16BIT_MSB * u_msb + NLM_16BIT_LSB * u_lsb;                                              \n" \
"       float v     = NLM_16BIT_MSB * v_msb + NLM_16BIT_LSB * v_lsb;                                              \n" \
"       write_imagef(U1, p, (float4) (y, u, v, 0.0f));                                                            \n" \
"   } else if (CHECK_FLAG(NLM_CLIP_TYPE_UNORM | NLM_CLIP_REF_RGB)) {                                              \n" \
"       float r     = read_imagef(R, smp, s).x;                                                                   \n" \
"       float g     = read_imagef(G, smp, s).x;                                                                   \n" \
"       float b     = read_imagef(B, smp, s).x;                                                                   \n" \
"       write_imagef(U1, p, (float4) (r, g, b, 0.0f));                                                            \n" \
"   } else if (CHECK_FLAG(NLM_CLIP_TYPE_UNSIGNED | NLM_CLIP_REF_RGB)) {                                           \n" \
"       float r     = native_divide(convert_float(read_imageui(R, smp, s).x), 1023.0f);                           \n" \
"       float g     = native_divide(convert_float(read_imageui(G, smp, s).x), 1023.0f);                           \n" \
"       float b     = native_divide(convert_float(read_imageui(B, smp, s).x), 1023.0f);                           \n" \
"       write_imagef(U1, p, (float4) (r, g, b, 0.0f));                                                            \n" \
"   }                                                                                                             \n" \
"}                                                                                                                \n" \
"                                                                                                                 \n" \
"__kernel                                                                                                         \n" \
"void nlmUnpack(__write_only image2d_t R, __write_only image2d_t G, __write_only image2d_t B,                     \n" \
"__write_only image2d_t R_lsb, __write_only image2d_t G_lsb, __write_only image2d_t B_lsb,                        \n" \
"__read_only image2d_t U1) {                                                                                      \n" \
"                                                                                                                 \n" \
"   int x = get_global_id(0);                                                                                     \n" \
"   int y = get_global_id(1);                                                                                     \n" \
"   if (x >= VI_DIM_X || y >= VI_DIM_Y) return;                                                                   \n" \
"                                                                                                                 \n" \
"   const sampler_t smp = CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_NONE | CLK_FILTER_NEAREST;                    \n" \
"   int2 s = (int2) (x, y);                                                                                       \n" \
"                                                                                                                 \n" \
"   if (CHECK_FLAG(NLM_CLIP_TYPE_STACKED | NLM_CLIP_REF_LUMA)) {                                                  \n" \
"       float  val = read_imagef(U1, smp, s).x;                                                                   \n" \
"       ushort y   = convert_ushort_sat(val * 65535.0f);                                                          \n" \
"       write_imageui(R,     s, (uint4)  (y >> CHAR_BIT, 0u, 0u, 0u));                                            \n" \
"       write_imageui(R_lsb, s, (uint4)  (y &  0xFF,     0u, 0u, 0u));                                            \n" \
"   } else if (CHECK_FLAG(NLM_CLIP_TYPE_UNORM | NLM_CLIP_REF_CHROMA)) {                                           \n" \
"       float2 val = read_imagef(U1, smp, s).xy;                                                                  \n" \
"       write_imagef(R,      s, (float4) (val.x, 0.0f, 0.0f, 0.0f));                                              \n" \
"       write_imagef(G,      s, (float4) (val.y, 0.0f, 0.0f, 0.0f));                                              \n" \
"   } else if (CHECK_FLAG(NLM_CLIP_TYPE_STACKED | NLM_CLIP_REF_CHROMA)) {                                         \n" \
"       float2 val = read_imagef(U1, smp, s).xy;                                                                  \n" \
"       ushort u   = convert_ushort_sat(val.x * 65535.0f);                                                        \n" \
"       ushort v   = convert_ushort_sat(val.y * 65535.0f);                                                        \n" \
"       write_imageui(R,     s, (uint4)  (u >> CHAR_BIT, 0u, 0u, 0u));                                            \n" \
"       write_imageui(G,     s, (uint4)  (v >> CHAR_BIT, 0u, 0u, 0u));                                            \n" \
"       write_imageui(R_lsb, s, (uint4)  (u &  0xFF,     0u, 0u, 0u));                                            \n" \
"       write_imageui(G_lsb, s, (uint4)  (v &  0xFF,     0u, 0u, 0u));                                            \n" \
"   } else if (CHECK_FLAG(NLM_CLIP_TYPE_UNORM | NLM_CLIP_REF_YUV)) {                                              \n" \
"       float3 val = read_imagef(U1, smp, s).xyz;                                                                 \n" \
"       write_imagef(R,      s, (float4) (val.x, 0.0f, 0.0f, 0.0f));                                              \n" \
"       write_imagef(G,      s, (float4) (val.y, 0.0f, 0.0f, 0.0f));                                              \n" \
"       write_imagef(B,      s, (float4) (val.z, 0.0f, 0.0f, 0.0f));                                              \n" \
"   } else if (CHECK_FLAG(NLM_CLIP_TYPE_UNSIGNED | NLM_CLIP_REF_YUV)) {                                           \n" \
"       float3 val = read_imagef(U1, smp, s).xyz;                                                                 \n" \
"       ushort y   = convert_ushort_sat(val.x * 1023.0f);                                                         \n" \
"       ushort u   = convert_ushort_sat(val.y * 1023.0f);                                                         \n" \
"       ushort v   = convert_ushort_sat(val.z * 1023.0f);                                                         \n" \
"       write_imageui(R,     s, (uint4)  (y, 0u, 0u, 0u));                                                        \n" \
"       write_imageui(G,     s, (uint4)  (u, 0u, 0u, 0u));                                                        \n" \
"       write_imageui(B,     s, (uint4)  (v, 0u, 0u, 0u));                                                        \n" \
"   } else if (CHECK_FLAG(NLM_CLIP_TYPE_STACKED | NLM_CLIP_REF_YUV)) {                                            \n" \
"       float3 val = read_imagef(U1, smp, s).xyz;                                                                 \n" \
"       ushort y   = convert_ushort_sat(val.x * 65535.0f);                                                        \n" \
"       ushort u   = convert_ushort_sat(val.y * 65535.0f);                                                        \n" \
"       ushort v   = convert_ushort_sat(val.z * 65535.0f);                                                        \n" \
"       write_imageui(R,     s, (uint4)  (y >> CHAR_BIT, 0u, 0u, 0u));                                            \n" \
"       write_imageui(G,     s, (uint4)  (u >> CHAR_BIT, 0u, 0u, 0u));                                            \n" \
"       write_imageui(B,     s, (uint4)  (v >> CHAR_BIT, 0u, 0u, 0u));                                            \n" \
"       write_imageui(R_lsb, s, (uint4)  (y &  0xFF,     0u, 0u, 0u));                                            \n" \
"       write_imageui(G_lsb, s, (uint4)  (u &  0xFF,     0u, 0u, 0u));                                            \n" \
"       write_imageui(B_lsb, s, (uint4)  (v &  0xFF,     0u, 0u, 0u));                                            \n" \
"   } else if (CHECK_FLAG(NLM_CLIP_TYPE_UNORM | NLM_CLIP_REF_RGB)) {                                              \n" \
"       float3  val    = read_imagef(U1, smp, s).xyz;                                                             \n" \
"       write_imagef(R,      s, (float4) (val.x, 0.0f, 0.0f, 0.0f));                                              \n" \
"       write_imagef(G,      s, (float4) (val.y, 0.0f, 0.0f, 0.0f));                                              \n" \
"       write_imagef(B,      s, (float4) (val.z, 0.0f, 0.0f, 0.0f));                                              \n" \
"   } else if (CHECK_FLAG(NLM_CLIP_TYPE_UNSIGNED | NLM_CLIP_REF_RGB)) {                                           \n" \
"       float3 val = read_imagef(U1, smp, s).xyz;                                                                 \n" \
"       ushort r   = convert_ushort(val.x * 1023.0f);                                                             \n" \
"       ushort g   = convert_ushort(val.y * 1023.0f);                                                             \n" \
"       ushort b   = convert_ushort(val.z * 1023.0f);                                                             \n" \
"       write_imageui(R,     s, (uint4)  (r, 0u, 0u, 0u));                                                        \n" \
"       write_imageui(G,     s, (uint4)  (g, 0u, 0u, 0u));                                                        \n" \
"       write_imageui(B,     s, (uint4)  (b, 0u, 0u, 0u));                                                        \n" \
"   }                                                                                                             \n" \
"}                                                                                                                ";
