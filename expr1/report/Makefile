TEX=report.tex
OUTDIR=output
LATEXMK=latexmk

.PHONY: all pdf clean

all: pdf

pdf:
	mkdir -p $(OUTDIR)
	$(LATEXMK) -xelatex -interaction=nonstopmode -halt-on-error -outdir=$(OUTDIR) $(TEX)

clean:
	$(LATEXMK) -C -outdir=$(OUTDIR)
