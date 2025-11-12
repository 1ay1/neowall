# IPC Library - shared between daemon and client

ipc_sources = files(
  'protocol.c',
  'socket_server.c',
  'socket_client.c',
)

ipc_headers = files(
  'protocol.h',
  'socket.h',
)

# IPC library (static)
libipc = static_library('neowall_ipc',
  ipc_sources,
  include_directories: inc_dirs,
  dependencies: [threads],
  install: false,
)

# Dependency object for other targets
ipc_dep = declare_dependency(
  link_with: libipc,
  include_directories: include_directories('.'),
)
