#!/bin/bash

mkdir -p ~/.vim/syntax >/dev/null
mkdir -p ~/.vim/ftdetect >/dev/null
cp vim/syntax/* ~/.vim/syntax/
cp vim/ftdetect/* ~/.vim/ftdetect/

