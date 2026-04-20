set -eu

pdflatex logo.tex
magick -density 512 logo.pdf -background none -resize 256x256 -gravity center -extent 256x256 logo.png
pdf2svg logo.tex logo.svg
