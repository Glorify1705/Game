import sys

name = sys.argv[3].encode('utf-8')

with open(sys.argv[2],'wb') as result_file:
  with open(sys.argv[1], 'rb') as datafile:
    data = datafile.read() 
    result_file.write(b"#ifndef _GAME_FONT_H\n")
    result_file.write(b"#define _GAME_FONT_H\n")
    result_file.write(b"\n#include <cstdint>\n")
    result_file.write(b"\nnamespace G {\n")
    result_file.write(b'inline constexpr uint8_t %s[] = {' % name)
    for b in open(sys.argv[1], 'rb').read():
        result_file.write(b'0x%02X,' % b)
    result_file.write(b'};')
    result_file.write(b"\n\n")
    result_file.write(b"constexpr size_t %sLength = %d;\n\n" % (name, len(data)))
    result_file.write(b"\n}\n")
    result_file.write(b"#endif")
