import os

project_source_dir = os.getenv("PROJECT_SOURCE_DIR")
directory = os.path.join(project_source_dir, 'src', 'shaders')
source_directory = os.path.join(project_source_dir, 'src')
vulkan_sdk = os.environ['VULKAN_SDK']
shader_compiler_path = os.path.join(vulkan_sdk,'Bin', 'glslc')

print('VULKAN_SDK=' + vulkan_sdk)
print('shaders directory=' + directory)

generated_file = open(os.path.join(source_directory,'gen_shaders.cxx'), 'w')
generated_file.write("#pragma once\n")
generated_file.write('#include <vector>\n\n')
    
for file in os.listdir(directory):
    filename = os.fsdecode(file)
    if filename.endswith('.vert') or filename.endswith('.frag'): 
        
        
        in_path = os.path.join(directory, filename)
        out_path = in_path.replace('.vert','.spv').replace('.frag','.spv')
        command = '{compiler} {fileIn} -o {fileOut}'.format(compiler=shader_compiler_path, fileIn=in_path, fileOut=out_path)
        print(command)
        os.system(command)
        f = open(out_path, 'rb')
        data = f.read()
           
        generated_file.write("static const std::vector<uint8_t> {} = {{".format(filename.replace('.vert','').replace('.frag','')))
        generated_file.write(", ".join(f"0x{b:02X}" for b in data))
        generated_file.write("};\n")
        