// A CPU read from a mapped sparse texture must not enter the submitter's upload batch.
#define nvn_game_fps nvn_game_fps_mtl_default
#include "../nvn/nvn_mtl.m"
#undef nvn_game_fps
#define g_ft g_ft_hle
#include "../nvn/hle_nvn.c"
#undef g_ft
uint64_t g_t0_ns;
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }

typedef struct { NVNtexture *texture; uint8_t pixels[128]; int partial; } read_case;
static int checks, failed;
#define CHECK(ok, label) do { int result = !!(ok); checks++; if (!result) failed++; printf("%s: %s\n", label, result ? "PASS" : "FAIL"); } while (0)
static void *worker_read(void *arg) { @autoreleasepool {
    pthread_setname_np("worker-readback-test");
    read_case *c = arg;
    NVNcopyRegion region = {0, 0, 0, 8, 4, 1};
    if (c->partial) {
        region = (NVNcopyRegion){2, 1, 0, 3, 2, 1};
        h_nvnTextureReadTexelsStrided(c->texture, NULL, &region, c->pixels, 32, 64);
    } else h_nvnTextureReadTexels(c->texture, NULL, &region, c->pixels);
    return NULL;
} }
static void read_and_check(NVNtexture *texture, const uint8_t expected[4], const char *label) {
    read_case c = {.texture=texture};
    pthread_t worker;
    if (pthread_create(&worker, NULL, worker_read, &c) || pthread_join(worker, NULL)) abort();
    int bad = 0;
    for (size_t i = 0; i < sizeof c.pixels; i += 4) bad += memcmp(c.pixels + i, expected, 4) != 0;
    CHECK(!bad, label);
}
int main(void) { @autoreleasepool {
    mtl_init();
    size_t len = 4 * getpagesize();
    uint8_t *mem = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    if (mem == MAP_FAILED) return 2;
    static const uint8_t literal[] = {17, 34, 51, 68};
    for (size_t i = 0; i < len; i += sizeof literal) memcpy(mem + i, literal, sizeof literal);
    pool_t physical, sparse;
    pool_builder_t pb = {.mem=mem, .size=len, .flags=0x24};
    if (!h_nvnMemoryPoolInitialize((NVNmemoryPool *)&physical, (const NVNmemoryPoolBuilder *)&pb)) return 2;
    pb = (pool_builder_t){.size=DM_GAME_APP_POOL + 2 * len, .flags=NVN_MEMORY_POOL_FLAGS_VIRTUAL | NVN_MEMORY_POOL_FLAGS_CPU_NO_ACCESS};
    if (!h_nvnMemoryPoolInitialize((NVNmemoryPool *)&sparse, (const NVNmemoryPoolBuilder *)&pb)) return 2;
    map_req map = {.phys=(NVNmemoryPool *)&physical, .size=len};
    if (!h_nvnMemoryPoolMapVirtual((NVNmemoryPool *)&sparse, 1, (const NVNmappingRequest *)&map)) return 2;
    NVNtextureBuilder tb; NVNtexture texture;
    h_nvnTextureBuilderSetDefaults(&tb);
    h_nvnTextureBuilderSetWidth(&tb, 8); h_nvnTextureBuilderSetHeight(&tb, 4);
    h_nvnTextureBuilderSetFormat(&tb, NVN_FORMAT_RGBA8);
    h_nvnTextureBuilderSetStorage(&tb, (NVNmemoryPool *)&sparse, 0);
    if (!h_nvnTextureInitialize(&texture, &tb)) return 2;
    mtl_sub_begin(); mtl_wait(mtl_sub_commit(), 5000000000ull);
    read_and_check(&texture, literal, "worker reads literal sparse pixels without an upload");
    map.phys = NULL;
    CHECK(h_nvnMemoryPoolMapVirtual((NVNmemoryPool *)&sparse, 1, (const NVNmappingRequest *)&map), "sparse texture unmapped");
    static const uint8_t zero[] = {0, 0, 0, 0};
    read_and_check(&texture, zero, "worker reads unmapped sparse pages as zero");
    static const uint8_t changed[] = {85, 102, 119, 136};
    for (size_t i = 0; i < len; i += sizeof changed) memcpy(mem + i, changed, sizeof changed);
    map.phys = (NVNmemoryPool *)&physical;
    CHECK(h_nvnMemoryPoolMapVirtual((NVNmemoryPool *)&sparse, 1, (const NVNmappingRequest *)&map), "sparse texture remapped");
    read_and_check(&texture, changed, "worker observes remapped physical content");
    read_case partial = {.texture=&texture, .partial=1};
    memset(partial.pixels, 0xEE, sizeof partial.pixels);
    pthread_t reader;
    if (pthread_create(&reader, NULL, worker_read, &partial) || pthread_join(reader, NULL)) abort();
    int bad = 0;
    for (size_t i = 0; i < sizeof partial.pixels; i++) {
        uint8_t expected = (i < 12 || (i >= 32 && i < 44)) ? changed[i % 4] : 0xEE;
        bad += partial.pixels[i] != expected;
    }
    CHECK(!bad, "worker partial sparse read preserves row padding and untouched bytes");
    h_nvnTextureFinalize(&texture);
    h_nvnMemoryPoolFinalize((NVNmemoryPool *)&sparse);
    // A real GPU render target must keep using GPU readback, not its stale guest bytes.
    h_nvnTextureBuilderSetStorage(&tb, (NVNmemoryPool *)&physical, 0);
    h_nvnTextureBuilderSetFlags(&tb, NVN_TEXTURE_FLAGS_COMPRESSIBLE);
    CHECK(h_nvnTextureInitialize(&texture, &tb), "GPU target initialized");
    id<MTLTexture> gpu = tex_mtl((const tex_t *)&texture);
    id<MTLCommandBuffer> cb = q_cb();
    MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = gpu;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(0.2, 0.4, 0.6, 1.0);
    [[cb renderCommandEncoderWithDescriptor:pass] endEncoding]; [cb commit]; [cb waitUntilCompleted];
    CHECK(cb.status == MTLCommandBufferStatusCompleted, "GPU clear completed");
    static const uint8_t rendered[] = {51, 102, 153, 255};
    read_and_check(&texture, rendered, "worker reads GPU results instead of stale guest bytes");
    h_nvnTextureFinalize(&texture);
    h_nvnMemoryPoolFinalize((NVNmemoryPool *)&physical);
    munmap(mem, len);
    printf("WORKER_READBACK checks=%d failed=%d\n", checks, failed);
    return failed != 0;
} }
