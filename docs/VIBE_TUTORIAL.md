# VIBE Configuration Tutorial

**VIBE** (Values In Bracket Expression) is the configuration format used by Staticwall. It's simpler and cleaner than TOML or YAML.

## Why VIBE?

- ✅ No quotes needed for simple strings
- ✅ No equals signs
- ✅ Flexible whitespace
- ✅ Clean, readable syntax
- ✅ Easy to parse

## 5-Minute Tutorial

### Basic Syntax

```vibe
# Comments start with #
key value
another_key another_value
```

### Objects (Nested Sections)

```vibe
section {
  key value
  another_key another_value
}
```

### Arrays (Lists)

```vibe
list [
  item1
  item2
  item3
]
```

### Complete Example

```vibe
# Application settings
app {
  name MyApp
  version 1.0
  debug false
}

# Server configuration
server {
  host localhost
  port 8080
  
  ssl {
    enabled true
    cert /etc/ssl/cert.pem
  }
}

# List of servers
servers [
  prod1.example.com
  prod2.example.com
  prod3.example.com
]
```

## Data Types

### Strings

```vibe
# No quotes needed
simple_string hello
path /home/user/file.txt
home_path ~/Documents/file.txt

# Spaces in values work fine
path_with_spaces ~/My Documents/My File.txt
```

### Numbers

```vibe
# Integers
count 42
negative -10

# Floats
price 19.99
temperature -5.5
```

### Booleans

```vibe
enabled true
disabled false
```

## Staticwall-Specific Examples

### Minimal Configuration

```vibe
default {
  path ~/Pictures/wallpaper.png
  mode fill
}
```

### Multi-Monitor Setup

```vibe
default {
  path ~/Pictures/default.png
  mode fill
}

output {
  eDP-1 {
    path ~/Pictures/laptop.jpg
    mode fill
  }
  
  HDMI-A-1 {
    path ~/Pictures/monitor.png
    mode fit
  }
}
```

### Wallpaper Cycling

```vibe
default {
  path ~/Pictures/wallpaper1.png
  mode fill
  duration 300
  transition fade
  cycle [
    ~/Pictures/wallpaper1.png
    ~/Pictures/wallpaper2.jpg
    ~/Pictures/wallpaper3.png
  ]
}
```

## Common Patterns

### Nested Configuration

```vibe
parent {
  child1 {
    key value
  }
  
  child2 {
    key value
  }
}
```

### Mixed Arrays

```vibe
# Arrays can contain different types
mixed [
  string_value
  42
  3.14
  true
]
```

### Multiple Top-Level Sections

```vibe
section1 {
  key value
}

section2 {
  key value
}

section3 {
  key value
}
```

## Comparison: VIBE vs TOML

### TOML

```toml
[default]
path "~/Pictures/wallpaper.png"
mode fill

[output.eDP-1]
path "~/Pictures/laptop.jpg"
mode fill
```

### VIBE

```vibe
default {
  path ~/Pictures/wallpaper.png
  mode fill
}

output {
  eDP-1 {
    path ~/Pictures/laptop.jpg
    mode fill
  }
}
```

**Benefits:**
- ✅ No quotes needed
- ✅ No equals signs
- ✅ No table notation confusion
- ✅ Clear nesting with braces
- ✅ More readable

## Rules to Remember

1. **Keys and values** are separated by whitespace
2. **Objects** use `{ }` braces
3. **Arrays** use `[ ]` brackets
4. **Comments** start with `#`
5. **Newlines** separate key-value pairs
6. **No quotes** needed for simple strings (paths, identifiers, etc.)

## When Do I Need Quotes?

In VIBE, you almost never need quotes! These all work without quotes:

```vibe
# Paths
path ~/Pictures/my file.png
path /home/user/Documents/wallpaper.jpg

# Values with spaces
name My Application Name
description A simple app

# URLs
url https://example.com/path

# Special characters (most of them)
value with-dashes-and_underscores
```

## Troubleshooting

### Common Mistakes

❌ **Wrong:**
```vibe
key value  # No equals sign needed
key: value   # No colon needed
key "value"  # No quotes needed
```

✅ **Correct:**
```vibe
key value
```

❌ **Wrong:**
```vibe
section {
  key value
# Missing closing brace
```

✅ **Correct:**
```vibe
section {
  key value
}
```

❌ **Wrong:**
```vibe
array [
  item1,  # No commas in VIBE
  item2,
  item3
]
```

✅ **Correct:**
```vibe
array [
  item1
  item2
  item3
]
```

## Quick Reference

```vibe
# Basic key-value
key value

# Object
section {
  nested_key nested_value
}

# Array
list [
  item1
  item2
]

# Numbers
integer 42
float 3.14
negative -10

# Booleans
flag true
disabled false

# Comments
# This is a comment
```

## Resources

- Full VIBE specification: `include/vibe.h` in the Staticwall source
- Staticwall config guide: `CONFIG_GUIDE.md`
- Example configs: `config/staticwall.vibe`

## Summary

VIBE makes configuration files simple and readable:

- **Write naturally** - no complex syntax rules
- **Easy to learn** - 5 minutes to master
- **Clean structure** - clear visual hierarchy
- **No surprises** - what you see is what you get

Start with the basics and build up as needed. VIBE gets out of your way and lets you focus on your configuration, not the format.

**Remember:** If it looks right, it probably is right. 🌊