import os

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

with open(os.path.join(root, 'world_files.txt'), 'r', encoding='utf-8-sig') as f:
    world_files = [line.strip().replace('\\', '/').lstrip('\ufeff') for line in f if line.strip()]

with open(os.path.join(root, 'Minecraft.World', 'sources.cmake'), 'w', encoding='utf-8') as f:
    f.write('set(WORLD_SOURCES\n')
    for wf in world_files:
        f.write(f'    "${{CMAKE_CURRENT_SOURCE_DIR}}/Minecraft.World/{wf}"\n')
    f.write(')\n')

with open(os.path.join(root, 'client_files.txt'), 'r', encoding='utf-8-sig') as f:
    client_files = [line.strip().replace('\\', '/').lstrip('\ufeff') for line in f if line.strip()]

with open(os.path.join(root, 'Minecraft.Client', 'sources.cmake'), 'w', encoding='utf-8') as f:
    f.write('set(CLIENT_SOURCES\n')
    for cf in client_files:
        f.write(f'    "${{CMAKE_CURRENT_SOURCE_DIR}}/Minecraft.Client/{cf}"\n')
    f.write(')\n')

print(f'Done: {len(world_files)} world files, {len(client_files)} client files')
