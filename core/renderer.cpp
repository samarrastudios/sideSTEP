#include "thumb_core.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace stepcore {
namespace {

struct V3 { float x, y, z; };
inline V3 sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 norm(V3 a) {
    float l = std::sqrt(dot(a, a));
    return l > 1e-20f ? V3{a.x / l, a.y / l, a.z / l} : V3{0, 0, 0};
}

}  // namespace

void RenderIso(const Mesh& mesh, int size, Image& out) {
    out.width = out.height = size;
    out.bgra.assign((size_t)size * size * 4, 0);
    if (mesh.tris.empty() || size <= 0) return;

    // Supersample for anti-aliasing; small thumbnails can afford more.
    const int ss = size <= 96 ? 4 : size <= 256 ? 3 : 2;
    const int W = size * ss, H = size * ss;

    // Classic CAD isometric: Z up, looking from front-right-top.
    const V3 eyeDir = norm({1.0f, -1.0f, 0.82f});  // from model toward camera
    const V3 up = {0, 0, 1};
    const V3 right = norm(cross(up, eyeDir));
    const V3 camUp = cross(eyeDir, right);

    // Fit the projected bounding box.
    float minX = 1e30f, maxX = -1e30f, minY = 1e30f, maxY = -1e30f, minD = 1e30f, maxD = -1e30f;
    for (int i = 0; i < 8; ++i) {
        V3 c = {(i & 1) ? mesh.bmax[0] : mesh.bmin[0], (i & 2) ? mesh.bmax[1] : mesh.bmin[1],
                (i & 4) ? mesh.bmax[2] : mesh.bmin[2]};
        float x = dot(c, right), y = dot(c, camUp), d = dot(c, eyeDir);
        minX = std::min(minX, x); maxX = std::max(maxX, x);
        minY = std::min(minY, y); maxY = std::max(maxY, y);
        minD = std::min(minD, d); maxD = std::max(maxD, d);
    }
    const float spanX = std::max(maxX - minX, 1e-12f), spanY = std::max(maxY - minY, 1e-12f);
    const float margin = 0.06f;
    const float scale = (1.0f - 2 * margin) * std::min(W / spanX, H / spanY);
    const float offX = 0.5f * W - 0.5f * (minX + maxX) * scale;
    const float offY = 0.5f * H + 0.5f * (minY + maxY) * scale;
    const float depthSpan = std::max(maxD - minD, 1e-12f);

    std::vector<float> zbuf((size_t)W * H, -std::numeric_limits<float>::infinity());
    std::vector<float> nbuf((size_t)W * H * 3, 0.0f);   // view-space normals for edge detection
    std::vector<float> cbuf((size_t)W * H * 3, 0.0f);   // shaded color

    const V3 light = norm({0.35f, 0.45f, 1.0f});  // in camera space (x right, y up, z toward eye)
    const V3 halfV = norm({light.x, light.y, light.z + 1.0f});

    for (const Triangle& t : mesh.tris) {
        float sx[3], sy[3], sd[3];
        V3 vn[3];
        V3 P[3];
        for (int k = 0; k < 3; ++k) {
            P[k] = {t.p[k][0], t.p[k][1], t.p[k][2]};
            sx[k] = dot(P[k], right) * scale + offX;
            sy[k] = offY - dot(P[k], camUp) * scale;
            sd[k] = dot(P[k], eyeDir);
        }
        const float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
        if (std::fabs(area) < 1e-12f) continue;
        const V3 faceN = norm(cross(sub(P[1], P[0]), sub(P[2], P[0])));
        for (int k = 0; k < 3; ++k) {
            V3 n = {t.n[k][0], t.n[k][1], t.n[k][2]};
            if (dot(n, n) < 0.25f) n = faceN;
            // To camera space.
            vn[k] = {dot(n, right), dot(n, camUp), dot(n, eyeDir)};
        }
        int x0 = std::max(0, (int)std::floor(std::min({sx[0], sx[1], sx[2]})));
        int x1 = std::min(W - 1, (int)std::ceil(std::max({sx[0], sx[1], sx[2]})));
        int y0 = std::max(0, (int)std::floor(std::min({sy[0], sy[1], sy[2]})));
        int y1 = std::min(H - 1, (int)std::ceil(std::max({sy[0], sy[1], sy[2]})));
        if (x0 > x1 || y0 > y1) continue;
        const float inv = 1.0f / area;
        const float base[3] = {t.rgb[0] / 255.0f, t.rgb[1] / 255.0f, t.rgb[2] / 255.0f};
        for (int y = y0; y <= y1; ++y) {
            const float py = y + 0.5f;
            for (int x = x0; x <= x1; ++x) {
                const float px = x + 0.5f;
                float w0 = ((sx[1] - px) * (sy[2] - py) - (sx[2] - px) * (sy[1] - py)) * inv;
                float w1 = ((sx[2] - px) * (sy[0] - py) - (sx[0] - px) * (sy[2] - py)) * inv;
                float w2 = 1.0f - w0 - w1;
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                const float d = w0 * sd[0] + w1 * sd[1] + w2 * sd[2];
                const size_t i = (size_t)y * W + x;
                if (d <= zbuf[i]) continue;
                zbuf[i] = d;
                V3 n = norm({w0 * vn[0].x + w1 * vn[1].x + w2 * vn[2].x,
                             w0 * vn[0].y + w1 * vn[1].y + w2 * vn[2].y,
                             w0 * vn[0].z + w1 * vn[1].z + w2 * vn[2].z});
                if (n.z < 0) n = {-n.x, -n.y, -n.z};  // two-sided: STEP faces are not always outward
                const float diff = std::max(0.0f, dot(n, light));
                const float spec = std::pow(std::max(0.0f, dot(n, halfV)), 40.0f) * 0.18f;
                const float lit = 0.32f + 0.72f * diff;
                for (int c = 0; c < 3; ++c) cbuf[i * 3 + c] = std::min(1.0f, base[c] * lit + spec);
                nbuf[i * 3 + 0] = n.x; nbuf[i * 3 + 1] = n.y; nbuf[i * 3 + 2] = n.z;
            }
        }
    }

    // Edge pass: darken silhouettes and creases so parts read at small sizes.
    std::vector<float> edge((size_t)W * H, 0.0f);
    const float depthTol = depthSpan * 0.012f;
    const int r = std::max(1, ss / 2);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const size_t i = (size_t)y * W + x;
            if (!std::isfinite(zbuf[i])) continue;
            float e = 0.0f;
            const int nx[4] = {x - r, x + r, x, x};
            const int ny[4] = {y, y, y - r, y + r};
            for (int k = 0; k < 4; ++k) {
                if (nx[k] < 0 || ny[k] < 0 || nx[k] >= W || ny[k] >= H) { e = std::max(e, 1.0f); continue; }
                const size_t j = (size_t)ny[k] * W + nx[k];
                if (!std::isfinite(zbuf[j])) { e = std::max(e, 1.0f); continue; }
                // Only the nearer surface draws the line (avoids double-thick edges).
                if (zbuf[i] - zbuf[j] > depthTol) e = std::max(e, 1.0f);
                const float nd = nbuf[i * 3] * nbuf[j * 3] + nbuf[i * 3 + 1] * nbuf[j * 3 + 1] + nbuf[i * 3 + 2] * nbuf[j * 3 + 2];
                if (nd < 0.80f) e = std::max(e, 0.55f);
            }
            edge[i] = e;
        }
    }

    // Downsample to premultiplied BGRA.
    const float invN = 1.0f / (ss * ss);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float acc[3] = {0, 0, 0}, cov = 0;
            for (int sy2 = 0; sy2 < ss; ++sy2)
                for (int sx2 = 0; sx2 < ss; ++sx2) {
                    const size_t i = (size_t)(y * ss + sy2) * W + (x * ss + sx2);
                    if (!std::isfinite(zbuf[i])) continue;
                    const float k = 1.0f - 0.62f * edge[i];
                    acc[0] += cbuf[i * 3 + 0] * k;
                    acc[1] += cbuf[i * 3 + 1] * k;
                    acc[2] += cbuf[i * 3 + 2] * k;
                    cov += 1.0f;
                }
            uint8_t* o = &out.bgra[((size_t)y * size + x) * 4];
            o[0] = (uint8_t)std::lround(std::min(1.0f, acc[2] * invN) * 255.0f);
            o[1] = (uint8_t)std::lround(std::min(1.0f, acc[1] * invN) * 255.0f);
            o[2] = (uint8_t)std::lround(std::min(1.0f, acc[0] * invN) * 255.0f);
            o[3] = (uint8_t)std::lround(cov * invN * 255.0f);
        }
    }
}

// ---- tiny PNG writer (stored deflate; fine for test output) ----
namespace {
uint32_t Crc(const uint8_t* d, size_t n, uint32_t crc = 0) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    crc = ~crc;
    for (size_t i = 0; i < n; ++i) crc = table[(crc ^ d[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}
void Put32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(x >> 24); v.push_back(x >> 16); v.push_back(x >> 8); v.push_back(x);
}
void Chunk(std::vector<uint8_t>& f, const char* type, const std::vector<uint8_t>& data) {
    Put32(f, (uint32_t)data.size());
    std::vector<uint8_t> td(type, type + 4);
    td.insert(td.end(), data.begin(), data.end());
    f.insert(f.end(), td.begin(), td.end());
    Put32(f, Crc(td.data(), td.size()));
}
}  // namespace

bool WritePng(const std::string& path, const Image& img) {
    std::vector<uint8_t> raw;
    raw.reserve((size_t)img.height * (img.width * 4 + 1));
    for (int y = 0; y < img.height; ++y) {
        raw.push_back(0);
        for (int x = 0; x < img.width; ++x) {
            const uint8_t* p = &img.bgra[((size_t)y * img.width + x) * 4];
            const int a = p[3];
            auto un = [a](int v) { return (uint8_t)(a ? std::min(255, v * 255 / a) : 0); };
            raw.push_back(un(p[2])); raw.push_back(un(p[1])); raw.push_back(un(p[0])); raw.push_back((uint8_t)a);
        }
    }
    std::vector<uint8_t> z = {0x78, 0x01};
    uint32_t a1 = 1, a2 = 0;
    for (uint8_t b : raw) { a1 = (a1 + b) % 65521; a2 = (a2 + a1) % 65521; }
    size_t pos = 0;
    do {
        const size_t n = std::min<size_t>(65535, raw.size() - pos);
        z.push_back(pos + n >= raw.size() ? 1 : 0);
        z.push_back(n & 0xFF); z.push_back(n >> 8); z.push_back(~n & 0xFF); z.push_back((~n >> 8) & 0xFF);
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
        pos += n;
    } while (pos < raw.size());
    Put32(z, (a2 << 16) | a1);

    std::vector<uint8_t> f = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<uint8_t> ihdr;
    Put32(ihdr, img.width); Put32(ihdr, img.height);
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
    Chunk(f, "IHDR", ihdr);
    Chunk(f, "IDAT", z);
    Chunk(f, "IEND", {});
    FILE* fp = fopen(path.c_str(), "wb");
    if (!fp) return false;
    const bool ok = fwrite(f.data(), 1, f.size(), fp) == f.size();
    fclose(fp);
    return ok;
}

}  // namespace stepcore
