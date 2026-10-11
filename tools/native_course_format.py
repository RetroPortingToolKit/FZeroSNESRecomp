"""Bounded readers for verified native F-Zero resource consumers."""
from audit_track_metadata import span


def native_terrain_properties(target, stock):
    """Translate the verified USA tile classifier to FZEdit properties.

    Skipping the separate mine-location bitmap does not disable terrain. Older
    projects still use $00:8E36 and $03:9187 to classify each sampled tile.
    Require the donor's classifiers, landing and recovery checks to be stock;
    custom classifiers need their own decoder rather than a guessed mapping.
    """
    for address, size in ((0x008e36, 0x98), (0x039187, 0x84),
                          (0x009c9a, 0x27), (0x00eb90, 0x1d)):
        actual = bytearray(span(target, address, size))
        # Fuzee-compatible revisions call the same classifier directly. The
        # original PHK/PER/JML trampoline and JSL/RTS return to the same caller;
        # recognize only this exact replacement, leaving every test intact.
        if address == 0x008e36 and actual[0x74:0x7d] == bytes.fromhex('22 87 91 03 60 ea ea ea ea'):
            actual[0x74:0x7d] = bytes.fromhex('4b 62 03 00 5c 87 91 03 60')
        if actual != span(stock, address, size):
            raise ValueError('Unsupported native terrain classifier')
    terrain = bytearray(0x400)
    for tile in range(256):
        # Native ground-height classes; the low flag bits also mark barriers.
        terrain[0x300+tile] = (16 if tile < 0x69 else 32 if tile < 0x82 or tile == 0xa5
                              else 128 if tile >= 0xd0 else 0)
        terrain[tile] = (128 if tile in (0x69, 0xc0) else 64 if tile in (0xa5, 0xc1) else 0)
        terrain[0x100+tile] = (128 if 0xa7 <= tile < 0xb0 or 0xcc <= tile < 0xd0
                              else 64 if 0xba <= tile < 0xc0
                              else 32 if 0xc8 <= tile < 0xcc
                              else 16 if 0xc3 <= tile < 0xc8
                              else 8 if tile == 0xa6
                              else 4 if tile in (0x9e, 0xa1, 0xa2) else 0)
        terrain[0x200+tile] = (128 if 0xb0 <= tile < 0xb9 else 16 if tile == 0xb9
                              else 64 if tile == 0xa0 else 32 if tile == 0x9c else 0)
    return bytes(terrain)


def decode_native_horizon(target, address, expected):
    """Bounded tile RLE used by the unchanged $03:9806 native decoder."""
    output=bytearray()
    for _ in range(expected//2+1):
        control=span(target,address,1)[0]
        if control==0xfc:
            if len(output)!=expected:
                raise ValueError('Native horizon has an unexpected decoded size')
            return bytes(output)
        kind=control&3
        if kind==3:
            value=b'\x80\x1d';count=(control>>2)+1;address+=1
        else:
            low=span(target,address+1,1)[0]
            value=bytes([low,(control|0x18) if kind==0 else (control&0xc4)|0x18])
            count=1 if kind==0 else ((control>>3)&7)+1
            address+=2
            if kind==2:
                count=span(target,address,1)[0]+1;address+=1
        if len(output)+count*2>expected:
            raise ValueError('Native horizon exceeds its tilemap bounds')
        output+=value*count
    raise ValueError('Unterminated native horizon stream')


