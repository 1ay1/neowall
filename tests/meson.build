# NeoWall Tests

test_sources = files(
    'test_config_rules.c',
)

# Configuration rules test
test_config_rules = executable(
    'test_config_rules',
    test_sources,
    include_directories: [
        include_directories('../include'),
        include_directories('../src'),
        include_directories('../src/neowalld'),
        include_directories('../src/neowalld/config'),
    ],
    dependencies: [
        dependency('threads'),
    ],
    link_with: [],
    c_args: [
        '-DTEST_BUILD',
        '-g',
        '-O0',
    ],
    build_by_default: false,
)

# Register test with meson
test('config_rules', test_config_rules, timeout: 30)

# Optional: add individual test suites
test('config_rules_validation', test_config_rules,
     args: ['--suite', 'validation'],
     suite: 'validation',
     timeout: 30)

test('config_rules_edge_cases', test_config_rules,
     args: ['--suite', 'edge_cases'],
     suite: 'edge_cases',
     timeout: 30)

test('config_rules_scenarios', test_config_rules,
     args: ['--suite', 'scenarios'],
     suite: 'scenarios',
     timeout: 30)
