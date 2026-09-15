#include <d3dcompiler.h>

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char* argv[])
{
    if (argc < 5)
    {
        std::cerr << "usage: shader_compiler <input> <entry> <output> <variable> [defines...]\n";
        return 2;
    }

    std::vector<std::string> names;
    names.reserve(argc - 5);
    for (int i = 5; i < argc; ++i)
        names.emplace_back(argv[i]);

    std::vector<D3D_SHADER_MACRO> macros;
    macros.reserve(names.size() + 1);
    for (const auto& name : names)
        macros.push_back({ name.c_str(), "1" });
    macros.push_back({ nullptr, nullptr });

    ID3DBlob* shader = nullptr;
    ID3DBlob* errors = nullptr;
    const auto input = std::filesystem::path(argv[1]).wstring();
    const HRESULT result = D3DCompileFromFile(
        input.c_str(), names.empty() ? nullptr : macros.data(), D3D_COMPILE_STANDARD_FILE_INCLUDE,
        argv[2], "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
        0, &shader, &errors);

    if (FAILED(result))
    {
        if (errors)
            std::cerr.write(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
        else
            std::cerr << "D3DCompileFromFile failed: 0x" << std::hex << result << '\n';
        if (errors)
            errors->Release();
        if (shader)
            shader->Release();
        return 1;
    }

    std::ofstream output(argv[3], std::ios::binary);
    if (!output)
    {
        std::cerr << "cannot create " << argv[3] << '\n';
        shader->Release();
        if (errors)
            errors->Release();
        return 1;
    }

    const auto* bytes = static_cast<const unsigned char*>(shader->GetBufferPointer());
    output << "#pragma once\n\nconst unsigned char " << argv[4] << "[] = {";
    for (size_t i = 0; i < shader->GetBufferSize(); ++i)
    {
        if (i % 16 == 0)
            output << '\n';
        output << static_cast<unsigned int>(bytes[i]) << ',';
    }
    output << "\n};\n";

    shader->Release();
    if (errors)
        errors->Release();
    return 0;
}
