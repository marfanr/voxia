#include <libk/jpeg.h>

// #include <stdint.h>
// #include <stdlib.h>
// #include <math.h>
// #include <string.h>
// #include <stdio.h>

// #define MAX_COMPONENT 3
// #define MAX_MCU 4096
// #define MAX_DQT 4
// #define MAX_HUFF 4

// typedef struct {
//     uint8_t id;
//     uint8_t h_samp;
//     uint8_t v_samp;
//     uint8_t q_table_id;
// } jpg_component_t;

// typedef struct {
//     uint16_t width;
//     uint16_t height;
//     uint8_t component_count;
//     jpg_component_t components[MAX_COMPONENT];
// } sof0_frame_t;

// typedef struct {
//     uint8_t id;
//     uint8_t precision; // 0=8bit, 1=16bit
//     uint16_t table[64];
// } dqt_t;

// typedef struct {
//     uint8_t bits[16];  // jumlah symbol per panjang kode
//     uint8_t values[256];
//     uint16_t code[256];
//     uint8_t size[256];
//     uint16_t count;
// } huffman_table_t;

// typedef struct {
//     uint8_t num_components;
//     struct {
//         uint8_t id;
//         uint8_t dc_table;
//         uint8_t ac_table;
//     } comp[MAX_COMPONENT];
//     uint8_t start_spectral;
//     uint8_t end_spectral;
//     uint8_t approx_high;
//     uint8_t approx_low;
// } sos_frame_t;

// typedef struct {
//     int16_t coef[64];
// } block_t;

// // Zigzag index
// static const uint8_t zigzag[64] = {
//     0,1,5,6,14,15,27,28,
//     2,4,7,13,16,26,29,42,
//     3,8,12,17,25,30,41,43,
//     9,11,18,24,31,40,44,53,
//     10,19,23,32,39,45,52,54,
//     20,22,33,38,46,51,55,60,
//     21,34,37,47,50,56,59,61,
//     35,36,48,49,57,58,62,63
// };

// // --- IDCT 8x8 ---
// void idct_block(int16_t input[64], uint8_t output[64]) {
//     for (int y=0;y<8;y++) {
//         for (int x=0;x<8;x++) {
//             double sum = 0.0;
//             for (int u=0;u<8;u++) {
//                 for (int v=0;v<8;v++) {
//                     double cu = (u==0)?1.0/sqrt(2):1.0;
//                     double cv = (v==0)?1.0/sqrt(2):1.0;
//                     double dct = (double)input[u*8+v];
//                     sum += cu*cv*dct*cos((2*x+1)*u*M_PI/16.0)*cos((2*y+1)*v*M_PI/16.0);
//                 }
//             }
//             sum *= 0.25;
//             sum += 128.0;
//             if(sum<0) sum=0; if(sum>255) sum=255;
//             output[y*8+x] = (uint8_t)sum;
//         }
//     }
// }

// // --- YCbCr -> RGB ---
// void ycbcr_to_rgb(uint8_t Y, uint8_t Cb, uint8_t Cr, uint8_t *R, uint8_t *G, uint8_t *B){
//     int r = Y + 1.402*(Cr-128);
//     int g = Y - 0.344136*(Cb-128) - 0.714136*(Cr-128);
//     int b = Y + 1.772*(Cb-128);
//     if(r<0) r=0; if(r>255) r=255;
//     if(g<0) g=0; if(g>255) g=255;
//     if(b<0) b=0; if(b>255) b=255;
//     *R=r; *G=g; *B=b;
// }

// // --- Contoh put_pixel (isi framebuffer) ---
// void put_pixel(int x,int y,uint32_t color,uint32_t *fb,int fb_width){
//     fb[y*fb_width + x] = color;
// }

// // --- Parsing JPEG dari file (minimal baseline) ---
// int parse_jpeg(const uint8_t *data, size_t size,
//                sof0_frame_t *frame,
//                dqt_t *dqts, int *dqts_count,
//                huffman_table_t *ht_dc, int *ht_dc_count,
//                huffman_table_t *ht_ac, int *ht_ac_count,
//                sos_frame_t *sos){
//     size_t idx=0;
//     *dqts_count=0; *ht_dc_count=0; *ht_ac_count=0;

//     while(idx<size){
//         if(data[idx]==0xFF){
//             uint8_t marker = data[idx+1];
//             if(marker==0xD8){ idx+=2; continue; } // SOI
//             else if(marker==0xE0){ // APP0
//                 uint16_t len = (data[idx+2]<<8)|data[idx+3];
//                 idx += len+2;
//             }
//             else if(marker==0xDB){ // DQT
//                 uint16_t len = (data[idx+2]<<8)|data[idx+3];
//                 size_t s = idx+4;
//                 while(s<idx+len+2){
//                     uint8_t pq_tq = data[s++];
//                     uint8_t pq = pq_tq>>4;
//                     uint8_t tq = pq_tq & 0xF;
//                     dqt_t *q = &dqts[(*dqts_count)++];
//                     q->id=tq; q->precision=pq;
//                     for(int i=0;i<64;i++){
//                         if(pq==0) q->table[i] = data[s++];
//                         else { q->table[i] = (data[s]<<8)|data[s+1]; s+=2; }
//                     }
//                 }
//                 idx+=len+2;
//             }
//             else if(marker==0xC0){ // SOF0 baseline
//                 uint16_t len = (data[idx+2]<<8)|data[idx+3];
//                 frame->component_count = data[idx+5];
//                 frame->height = (data[idx+3]<<8)|data[idx+4];
//                 frame->width  = (data[idx+1]<<8)|data[idx+2]; // perbaiki urutan sesuai JPEG
//                 for(int i=0;i<frame->component_count;i++){
//                     frame->components[i].id = data[idx+6+i*3];
//                     uint8_t samp = data[idx+7+i*3];
//                     frame->components[i].h_samp = samp>>4;
//                     frame->components[i].v_samp = samp&0xF;
//                     frame->components[i].q_table_id = data[idx+8+i*3];
//                 }
//                 idx+=len+2;
//             }
//             else if(marker==0xC4){ // DHT
//                 uint16_t len = (data[idx+2]<<8)|data[idx+3];
//                 // Parsing Huffman table bisa ditambahkan di sini
//                 idx+=len+2;
//             }
//             else if(marker==0xDA){ // SOS
//                 uint16_t len = (data[idx+2]<<8)|data[idx+3];
//                 // parsing SOS
//                 idx+=len+2;
//             }
//             else{ idx+=2; }
//         } else idx++;
//     }
//     return 0;
// }

// // --- Fungsi contoh render MCU (IDCT + YCbCr→RGB) ---
// void render_example(uint32_t *fb,int fb_width,int fb_height){
//     block_t Y, Cb, Cr;
//     for(int i=0;i<64;i++){ Y.coef[i]=rand()%256 -128; Cb.coef[i]=128; Cr.coef[i]=128; }
//     render_mcu(&Y,&Cb,&Cr,0,0,2,2,fb,fb_width);
// }

// int main(){
//     // --- framebuffer contoh ---
//     int width=320, height=240;
//     uint32_t *fb = malloc(width*height*sizeof(uint32_t));
//     memset(fb,0,sizeof(uint32_t)*width*height);

//     // --- parsing JPEG ---
//     FILE *f = fopen("test.jpg","rb");
//     fseek(f,0,SEEK_END); size_t sz = ftell(f); fseek(f,0,SEEK_SET);
//     uint8_t *buf = malloc(sz); fread(buf,1,sz,f); fclose(f);

//     sof0_frame_t frame;
//     dqt_t dqts[MAX_DQT];
//     huffman_table_t ht_dc[MAX_HUFF], ht_ac[MAX_HUFF];
//     int dqts_count, ht_dc_count, ht_ac_count;
//     sos_frame_t sos;

//     parse_jpeg(buf,sz,&frame,dqts,&dqts_count,ht_dc,&ht_dc_count,ht_ac,&ht_ac_count,&sos);

//     // --- contoh render ---
//     render_example(fb,width,height);

//     // --- tampilkan di konsol sebagai PPM ---
//     FILE *ppm = fopen("out.ppm","wb");
//     fprintf(ppm,"P6\n%d %d\n255\n",width,height);
//     for(int i=0;i<width*height;i++){
//         fputc((fb[i]>>16)&0xFF,ppm);
//         fputc((fb[i]>>8)&0xFF,ppm);
//         fputc(fb[i]&0xFF,ppm);
//     }
//     fclose(ppm);
// }
