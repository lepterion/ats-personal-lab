#!/usr/bin/env python3
"""Converts docs/REPORT.md into docs/REPORT.docx (title page + sections).

Supports the subset of Markdown used in the report: "# " title, "## " sections,
paragraphs, "- " lists, ``` code blocks, | tables |, "> " quotes and inline
**bold**, *italic*, `code`. The lines between the "# " title and the first
"## " heading form the title page.

Requires python-docx:  pip install python-docx
Usage: python3 tools/make_report_docx.py docs/REPORT.md docs/REPORT.docx
"""
import re
import sys

from docx import Document
from docx.enum.text import WD_ALIGN_PARAGRAPH, WD_BREAK
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from docx.shared import Pt, Cm, RGBColor

FONT = "Times New Roman"
MONO = "Consolas"
LANG = "uk-UA"
INLINE = re.compile(r"(\*\*[^*]+\*\*|`[^`]+`|\*[^*\s][^*]*\*)")


def set_font(run, name, size=None):
    run.font.name = name
    run._element.rPr.rFonts.set(qn("w:eastAsia"), name)
    if size:
        run.font.size = Pt(size)


def no_proof(run):
    """Code is not Ukrainian text: no spell checking."""
    run._element.get_or_add_rPr().append(OxmlElement("w:noProof"))


def set_language(style_or_defaults_rpr, lang=LANG):
    lang_el = style_or_defaults_rpr.find(qn("w:lang"))
    if lang_el is None:
        lang_el = OxmlElement("w:lang")
        style_or_defaults_rpr.append(lang_el)
    for attr in ("w:val", "w:eastAsia", "w:bidi"):
        lang_el.set(qn(attr), lang)


def plain_font(rpr, name):
    """Sets the font for all scripts and drops theme fonts (they override the name)."""
    fonts = rpr.find(qn("w:rFonts"))
    if fonts is None:
        fonts = OxmlElement("w:rFonts")
        rpr.insert(0, fonts)
    for attr in list(fonts.attrib):
        if attr.endswith("Theme"):
            del fonts.attrib[attr]
    for attr in ("w:ascii", "w:hAnsi", "w:eastAsia", "w:cs"):
        fonts.set(qn(attr), name)


def add_inline(par, text, size=None, bold=False, italic=False):
    text = text.replace("\\|", "|")
    for part in INLINE.split(text):
        if not part:
            continue
        if part.startswith("**") and part.endswith("**"):
            add_inline(par, part[2:-2], size, True, italic)   # may contain `code`
        elif part.startswith("`") and part.endswith("`"):
            run = par.add_run(part[1:-1])
            run.bold, run.italic = bold, italic
            set_font(run, MONO, (size or 12) - 1)
            no_proof(run)
        elif part.startswith("*") and part.endswith("*") and len(part) > 2:
            add_inline(par, part[1:-1], size, bold, True)
        else:
            run = par.add_run(part)
            run.bold, run.italic = bold, italic
            set_font(run, FONT, size)


def title_page(doc, lines):
    # lines: university, faculty, department, "", course, lab, topic, variant, "", author..., "", city
    lines = [l.strip() for l in lines]
    blocks, cur = [], []
    for l in lines:
        if l:
            cur.append(l)
        elif cur:
            blocks.append(cur)
            cur = []
    if cur:
        blocks.append(cur)
    header, middle, author, city = blocks[0], blocks[1], blocks[2], blocks[-1]

    # Exact gaps (in points) instead of empty paragraphs, so the page never
    # overflows. Usable height of A4 with 2 cm margins is about 728 pt.
    def line(text, align, size, before=0, bold=False):
        p = doc.add_paragraph()
        p.alignment = align
        p.paragraph_format.space_before = Pt(before)
        p.paragraph_format.space_after = Pt(0)
        p.paragraph_format.line_spacing = 1.15
        p.paragraph_format.first_line_indent = Cm(0)
        add_inline(p, text, size)
        if bold:
            for r in p.runs:
                r.bold = True
        return p

    for l in header:
        line(l, WD_ALIGN_PARAGRAPH.CENTER, 14)
    for i, l in enumerate(middle):
        line(l, WD_ALIGN_PARAGRAPH.CENTER, 16 if i == 2 else 14,
             before=170 if i == 0 else (6 if i == 2 else 0), bold=(i == 2))
    for i, l in enumerate(author):
        line(l, WD_ALIGN_PARAGRAPH.RIGHT, 14, before=140 if i == 0 else 0)
    p = line(city[0], WD_ALIGN_PARAGRAPH.CENTER, 14, before=140)
    p.add_run().add_break(WD_BREAK.PAGE)


def add_table(doc, rows):
    cells = [[c.strip() for c in re.split(r"(?<!\\)\|", r.strip().strip("|"))] for r in rows]
    cells = [r for r in cells if not all(re.fullmatch(r":?-{3,}:?", c) for c in r)]
    table = doc.add_table(rows=len(cells), cols=len(cells[0]))
    table.style = "Table Grid"
    for i, row in enumerate(cells):
        for j, text in enumerate(row):
            par = table.cell(i, j).paragraphs[0]
            add_inline(par, text, 11)
            if i == 0:
                for r in par.runs:
                    r.bold = True
    doc.add_paragraph()


def main(src, dst):
    lines = open(src, encoding="utf-8").read().splitlines()
    doc = Document()
    for s in doc.sections:
        s.page_width, s.page_height = Cm(21), Cm(29.7)   # A4 (the template is US Letter)
        s.left_margin, s.right_margin = Cm(2.5), Cm(1.5)
        s.top_margin = s.bottom_margin = Cm(2)
    normal = doc.styles["Normal"]
    normal.font.name = FONT
    normal.font.size = Pt(12)
    plain_font(normal.element.get_or_add_rPr(), FONT)
    normal.paragraph_format.space_after = Pt(6)
    # Ukrainian everywhere, otherwise Word/OnlyOffice spell-check it as English.
    defaults_rpr = doc.styles.element.find(qn("w:docDefaults")).find(qn("w:rPrDefault")).find(qn("w:rPr"))
    set_language(defaults_rpr)
    for style in doc.styles:
        rpr = style.element.find(qn("w:rPr"))
        if rpr is not None:
            set_language(rpr)
    # Headings: black Times New Roman instead of the blue theme font.
    for name in ("Heading 1", "Heading 2", "Title"):
        st = doc.styles[name]
        st.font.color.rgb = RGBColor(0, 0, 0)
        st.font.bold = True
        plain_font(st.element.get_or_add_rPr(), FONT)
    doc.styles["Heading 1"].font.size = Pt(16)
    doc.styles["Heading 1"].paragraph_format.space_before = Pt(18)
    doc.styles["Heading 1"].paragraph_format.space_after = Pt(6)

    first_section = next(i for i, l in enumerate(lines) if l.startswith("## "))
    title_page(doc, lines[1:first_section])

    i = first_section
    while i < len(lines):
        line = lines[i]
        if line.startswith("## "):
            h = doc.add_heading(level=1)
            run = h.add_run(line[3:])
            set_font(run, FONT, 16)
            i += 1
        elif line.startswith("```"):
            j = i + 1
            code = []
            while not lines[j].startswith("```"):
                code.append(lines[j])
                j += 1
            p = doc.add_paragraph()
            p.paragraph_format.left_indent = Cm(0.5)
            run = p.add_run("\n".join(code))
            set_font(run, MONO, 10)
            no_proof(run)
            i = j + 1
        elif line.startswith("|"):
            j = i
            while j < len(lines) and lines[j].startswith("|"):
                j += 1
            add_table(doc, lines[i:j])
            i = j
        elif line.startswith("- "):
            p = doc.add_paragraph(style="List Bullet")
            add_inline(p, line[2:])
            i += 1
        elif line.startswith("> "):
            p = doc.add_paragraph()
            p.paragraph_format.left_indent = Cm(1)
            add_inline(p, line[2:])
            for r in p.runs:
                r.italic = True
            i += 1
        elif re.match(r"^\d+\. ", line):
            p = doc.add_paragraph(style="List Number")
            add_inline(p, re.sub(r"^\d+\. ", "", line))
            i += 1
        elif line.strip() == "":
            i += 1
        else:
            p = doc.add_paragraph()
            p.paragraph_format.first_line_indent = Cm(1)
            add_inline(p, line)
            i += 1
    doc.save(dst)
    print(f"written {dst}")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
