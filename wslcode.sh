#!/bin/bash

echo $@ | sed 's/\\/\//g' | sed 's/\/\/wsl\$\/Ubuntu//' | xargs code
