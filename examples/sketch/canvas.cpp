// canvas.cpp - saving the picture as an IFF ILBM with iffparse.library.
//
//   FORM ILBM
//     BMHD  the size, the number of bitplanes, no compression
//     CMAP  the palette (red, green, blue bytes)
//     BODY  each row, bitplane by bitplane, 16-pixel aligned
#include "canvas.h"

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/iffparse.h>

const uint32 Canvas::palette[COLOURS] = {
    0xFFFFFFFF,   // white (the paper and the eraser)
    0xFF000000,   // black
    0xFFE02020,   // red
    0xFF20A040,   // green
    0xFF2050E0,   // blue
    0xFFF0D020,   // yellow
    0xFFF08020,   // orange
    0xFF9030C0,   // purple
};

static constexpr int32 makeId(char a, char b, char c, char d)
{
    return (int32)(((uint32)(uint8)a << 24) | ((uint32)(uint8)b << 16) | ((uint32)(uint8)c << 8) | (uint32)(uint8)d);
}
static const int32 idForm = makeId('F', 'O', 'R', 'M'), idIlbm = makeId('I', 'L', 'B', 'M'),
                   idBmhd = makeId('B', 'M', 'H', 'D'), idCmap = makeId('C', 'M', 'A', 'P'),
                   idBody = makeId('B', 'O', 'D', 'Y');

// An IFF file open for writing: AllocIFF, Open, InitIFFasDOS, OpenIFF - and back again.
class IffWriter {
public:
    explicit IffWriter(const std::string &path)
    {
        iff = IIFFParse->AllocIFF();
        if (!iff) return;
        file = IDOS->Open(path.c_str(), MODE_NEWFILE);
        if (!file) return;
        iff->iff_Stream = (uint32)file;
        IIFFParse->InitIFFasDOS(iff);
        opened = IIFFParse->OpenIFF(iff, IFFF_WRITE) == 0;
    }
    ~IffWriter()
    {
        if (opened) IIFFParse->CloseIFF(iff);
        if (file) IDOS->Close(file);
        if (iff) IIFFParse->FreeIFF(iff);
    }
    IffWriter(const IffWriter &) = delete;
    IffWriter &operator=(const IffWriter &) = delete;
    bool ok() const { return opened; }
    bool push(int32 type, int32 id, int32 size = IFFSIZE_UNKNOWN) { return IIFFParse->PushChunk(iff, type, id, size) == 0; }
    bool write(const void *data, int32 n) { return IIFFParse->WriteChunkBytes(iff, data, n) == n; }
    bool pop() { return IIFFParse->PopChunk(iff) == 0; }

private:
    struct IFFHandle *iff = nullptr;
    BPTR file = 0;
    bool opened = false;
};

static void put16(std::vector<uint8> &v, uint32 x) { v.push_back((uint8)(x >> 8)); v.push_back((uint8)x); }

bool Canvas::saveILBM(const std::string &path, std::string &error) const
{
    IffWriter out(path);
    if (!out.ok()) {
        error = "Can't create " + path;
        return false;
    }
    // BMHD, written byte by byte (big-endian, no padding questions)
    std::vector<uint8> bmhd;
    put16(bmhd, w); put16(bmhd, h);          // width, height
    put16(bmhd, 0); put16(bmhd, 0);          // x, y
    bmhd.push_back(DEPTH);                   // planes
    bmhd.push_back(0);                       // masking: none
    bmhd.push_back(0);                       // compression: none
    bmhd.push_back(0);                       // pad
    put16(bmhd, 0);                          // transparent colour
    bmhd.push_back(1); bmhd.push_back(1);    // x and y aspect: square pixels
    put16(bmhd, w); put16(bmhd, h);          // page size

    std::vector<uint8> cmap;
    for (uint32 c : palette) {
        cmap.push_back((uint8)(c >> 16));
        cmap.push_back((uint8)(c >> 8));
        cmap.push_back((uint8)c);
    }

    // BODY: per row, one line of each bitplane
    int rowBytes = ((w + 15) / 16) * 2;
    std::vector<uint8> body((size_t)rowBytes * DEPTH * h, 0);
    for (int y = 0; y < h; y++)
        for (int p = 0; p < DEPTH; p++) {
            uint8 *line = &body[((size_t)y * DEPTH + p) * rowBytes];
            for (int x = 0; x < w; x++)
                if (at(x, y) & (1 << p)) line[x >> 3] |= (uint8)(0x80 >> (x & 7));
        }

    bool ok = out.push(idIlbm, idForm) &&
              out.push(0, idBmhd, (int32)bmhd.size()) && out.write(bmhd.data(), (int32)bmhd.size()) && out.pop() &&
              out.push(0, idCmap, (int32)cmap.size()) && out.write(cmap.data(), (int32)cmap.size()) && out.pop() &&
              out.push(0, idBody, (int32)body.size()) && out.write(body.data(), (int32)body.size()) && out.pop() &&
              out.pop();
    if (!ok) error = "Couldn't write " + path + " (is the disk full?)";
    return ok;
}
