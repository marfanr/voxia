#include <libk/bits.h>

uint8_t
read_byte(bitstream_t *bs)
{

    size_t byte_pos = bs->bit_pos / 8;
    if (byte_pos > bs->size)
        return 0;

    uint8_t val = bs->data[byte_pos];
    if (val == 0xFF)
    {
        if (byte_pos + 1 < bs->size && bs->data[byte_pos + 1] == 0x00)
        {
            // Stuffed byte, skip 0x00
            bs->bit_pos += 16;
            return 0xFF;
        }
        else
        {
            // Marker encountered, stop reading
            // throw std::runtime_error("JPEG marker encountered in bitstream");
        }
    }
    else
    {
        bs->bit_pos += 8;
        return val;
    }
    return 0;
}

uint32_t
read_bits(bitstream_t *bs, int n)
{
    uint32_t val = 0;
    for (int i = 0; i < n; i++)
    {
        size_t byte_pos   = bs->bit_pos / 8;
        int    bit_offset = 7 - (bs->bit_pos % 8);

        if (byte_pos >= bs->size)
            return val; // EOF

        uint8_t byte = read_byte(bs);
        val          = (val << 1) | ((byte >> bit_offset) & 1);
    }
    return val;
}

uint8_t
read_bit(bitstream_t *bs)
{
    uint64_t byte_pos    = bs->bit_pos / 8;
    uint8_t  bit_in_byte = 7 - (bs->bit_pos % 8);
    uint8_t  b           = (bs->data[byte_pos] >> bit_in_byte) & 1;
    bs->bit_pos++;
    return b;
}
