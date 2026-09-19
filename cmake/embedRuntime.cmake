file(READ "${input}" runtimeText)
file(WRITE "${output}" "#pragma once\ninline constexpr char aeroRuntimeSource[] = R\"aeroEmbedded(${runtimeText})aeroEmbedded\";\n")
