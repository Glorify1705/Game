# Generates a C header embedding INPUT as a string constant named VARNAME.
file(READ "${INPUT}" FILE_CONTENTS)
string(REPLACE "\\" "\\\\" FILE_CONTENTS "${FILE_CONTENTS}")
string(REPLACE "\"" "\\\"" FILE_CONTENTS "${FILE_CONTENTS}")
string(REPLACE "\n" "\\n\"\n\"" FILE_CONTENTS "${FILE_CONTENTS}")
file(WRITE "${OUTPUT}"
"// Auto-generated from ${INPUT}. Do not edit.\n"
"#pragma once\n"
"static const char ${VARNAME}[] =\n"
"\"${FILE_CONTENTS}\";\n"
"static const unsigned int ${VARNAME}Len = sizeof(${VARNAME}) - 1;\n"
)
