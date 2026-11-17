#!/usr/bin/env python3
"""Converts docs/REPORT.md into docs/REPORT.docx.

Layout: title page (city and year pinned to the bottom via the first-page footer),
table of contents, then the sections. Page numbers in the footer of other pages.

Supported Markdown subset: "# " title, "## " sections (go to the contents),
"### " subsections, paragraphs, "- " and "1. " lists, ``` code blocks,
| tables |, "> " quotes, inline **bold**, *italic*, `code` (may be nested in
bold/italic). A list item "- `signature` — text" is laid out like in the
sample report: the signature on its own line, the description indented below.
The lines between the "# " title and the first "## " form the title page:
blocks separated by blank lines = header, topic, author, city.

The contents is a real TOC field (Word/OnlyOffice can update it). Page numbers
are filled in from a test render with the OnlyOffice converter x2t if it is
available (paths: env X2T, ONLYOFFICE_FONTS); otherwise the entries have no
numbers and the field has to be updated in the editor.

Requires python-docx:  pip install python-docx
Usage: python3 tools/make_report_docx.py docs/REPORT.md docs/REPORT.docx
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

from docx import Document
from docx.enum.text import WD_ALIGN_PARAGRAPH, WD_BREAK, WD_TAB_ALIGNMENT, WD_TAB_LEADER
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from docx.shared import Pt, Cm, RGBColor

FONT = "Times New Roman"
MONO = "Consolas"
LANG = "uk-UA"
BODY = 13          # body text size, pt
CODE_INLINE = 11   # inline code next to body text (Consolas looks bigger than Times)
CODE_BLOCK = 10
INLINE = re.compile(r"(\*\*[^*]+\*\*|`[^`]+`|\*[^*\s][^*]*\*)")
SIGNATURE_ITEM = re.compile(r"^- ((?:`[^`]+`(?:\s*[,/]\s*)?)+)\s+—\s+(.*)$")

X2T = os.environ.get("X2T", "/opt/onlyoffice/desktopeditors/converter/x2t")
OO_FONTS = os.environ.get(
    "ONLYOFFICE_FONTS", os.path.expanduser("~/.local/share/onlyoffice/desktopeditors/data/fonts"))


# ---------- low-level helpers ----------

def set_font(run, name, size=None):
    run.font.name = name
    run._element.rPr.rFonts.set(qn("w:eastAsia"), name)
    if size:
        run.font.size = Pt(size)


def no_proof(run):
    """Code is not Ukrainian text: no spell checking."""
    run._element.get_or_add_rPr().append(OxmlElement("w:noProof"))


def set_language(rpr, lang=LANG):
    el = rpr.find(qn("w:lang"))
    if el is None:
        el = OxmlElement("w:lang")
        rpr.append(el)
    for attr in ("w:val", "w:eastAsia", "w:bidi"):
        el.set(qn(attr), lang)


def plain_font(rpr, name):
    """Font for all scripts, without theme fonts (they override the name)."""
    fonts = rpr.find(qn("w:rFonts"))
    if fonts is None:
        fonts = OxmlElement("w:rFonts")
        rpr.insert(0, fonts)
    for attr in list(fonts.attrib):
        if attr.endswith("Theme"):
            del fonts.attrib[attr]
    for attr in ("w:ascii", "w:hAnsi", "w:eastAsia", "w:cs"):
        fonts.set(qn(attr), name)


def fld_char(par, kind):
    r = par.add_run()
    el = OxmlElement("w:fldChar")
    el.set(qn("w:fldCharType"), kind)
    r._element.append(el)


def instr_text(par, instr):
    r = par.add_run()
    it = OxmlElement("w:instrText")
    it.set(qn("xml:space"), "preserve")
    it.text = f" {instr} "
    r._element.append(it)


def add_field(par, instr, text="", size=None):
    """Complex field: begin, instruction, separate, cached result, end."""
    fld_char(par, "begin")
    instr_text(par, instr)
    fld_char(par, "separate")
    if text:
        r = par.add_run(text)
        set_font(r, FONT, size)
    fld_char(par, "end")


def add_inline(par, text, size=None, bold=False, italic=False, code_size=None):
    size = size or BODY
    code_size = code_size or (size - 2)
    text = text.replace("\\|", "|")
    for part in INLINE.split(text):
        if not part:
            continue
        if part.startswith("**") and part.endswith("**"):
            add_inline(par, part[2:-2], size, True, italic, code_size)
        elif part.startswith("`") and part.endswith("`"):
            run = par.add_run(part[1:-1])
            run.bold, run.italic = bold, italic
            set_font(run, MONO, code_size)
            no_proof(run)
        elif part.startswith("*") and part.endswith("*") and len(part) > 2:
            add_inline(par, part[1:-1], size, bold, True, code_size)
        else:
            run = par.add_run(part)
            run.bold, run.italic = bold, italic
            set_font(run, FONT, size)


def fmt(p, before=0, after=6, indent=None, first=None, keep=False, line=None):
    pf = p.paragraph_format
    pf.space_before, pf.space_after = Pt(before), Pt(after)
    if indent is not None:
        pf.left_indent = Cm(indent)
    if first is not None:
        pf.first_line_indent = Cm(first)
    if keep:
        pf.keep_with_next = True
    if line is not None:
        pf.line_spacing = line
    return p


# ---------- document parts ----------

def setup(doc):
    sec = doc.sections[0]
    sec.page_width, sec.page_height = Cm(21), Cm(29.7)   # A4 (the template is US Letter)
    sec.left_margin, sec.right_margin = Cm(2.5), Cm(1.5)
    sec.top_margin, sec.bottom_margin = Cm(2), Cm(2)
    sec.footer_distance = Cm(1.2)

    normal = doc.styles["Normal"]
    normal.font.name = FONT
    normal.font.size = Pt(BODY)
    plain_font(normal.element.get_or_add_rPr(), FONT)
    normal.paragraph_format.space_after = Pt(6)
    normal.paragraph_format.line_spacing = 1.15
    # Ukrainian everywhere, otherwise editors spell-check the text as English.
    defaults = doc.styles.element.find(qn("w:docDefaults")).find(qn("w:rPrDefault")).find(qn("w:rPr"))
    set_language(defaults)
    for style in doc.styles:
        rpr = style.element.find(qn("w:rPr"))
        if rpr is not None:
            set_language(rpr)
    # Headings: black Times New Roman instead of the blue theme font.
    for name, size, before, after in (("Heading 1", 16, 18, 8), ("Heading 2", 14, 12, 4)):
        st = doc.styles[name]
        st.font.color.rgb = RGBColor(0, 0, 0)
        st.font.bold = True
        st.font.italic = False
        st.font.size = Pt(size)
        plain_font(st.element.get_or_add_rPr(), FONT)
        st.paragraph_format.space_before = Pt(before)
        st.paragraph_format.space_after = Pt(after)
        st.paragraph_format.keep_with_next = True


def footers(doc, city):
    sec = doc.sections[0]
    sec.different_first_page_header_footer = True
    # Title page: city and year at the very bottom of the sheet.
    p = sec.first_page_footer.paragraphs[0]
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    add_inline(p, city, 14)
    # Other pages: page number.
    p = sec.footer.paragraphs[0]
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    add_field(p, "PAGE", "1", 12)


def title_page(doc, blocks):
    header, middle, author = blocks[0], blocks[1], blocks[2]
    for l in header:
        p = doc.add_paragraph()
        p.alignment = WD_ALIGN_PARAGRAPH.CENTER
        fmt(p, after=0)
        add_inline(p, l, 14)
    for i, l in enumerate(middle):
        p = doc.add_paragraph()
        p.alignment = WD_ALIGN_PARAGRAPH.CENTER
        fmt(p, before=200 if i == 0 else (6 if i == 2 else 0), after=0)
        add_inline(p, l, 18 if i == 2 else 14, bold=(i == 2))
    for i, l in enumerate(author):
        p = doc.add_paragraph()
        p.alignment = WD_ALIGN_PARAGRAPH.RIGHT
        fmt(p, before=160 if i == 0 else 0, after=0)
        add_inline(p, l, 14)
    doc.paragraphs[-1].add_run().add_break(WD_BREAK.PAGE)


def contents(doc, headings, pages):
    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    fmt(p, after=12)
    add_inline(p, "Зміст", 16, bold=True)

    sec = doc.sections[0]
    right = sec.page_width - sec.left_margin - sec.right_margin
    paras = []
    for h in headings:
        p = fmt(doc.add_paragraph(), after=4)
        p.paragraph_format.tab_stops.add_tab_stop(right, WD_TAB_ALIGNMENT.RIGHT, WD_TAB_LEADER.DOTS)
        paras.append((p, h))
    # One TOC field spanning all entry paragraphs; its cached result is our text.
    first, last = paras[0][0], paras[-1][0]
    fld_char(first, "begin")
    instr_text(first, 'TOC \\o "1-1" \\h \\z \\u')
    fld_char(first, "separate")
    for p, h in paras:
        add_inline(p, h, BODY)
        r = p.add_run("\t" + str(pages.get(h, "")))
        set_font(r, FONT, BODY)
    fld_char(last, "end")
    last.add_run().add_break(WD_BREAK.PAGE)


def add_table(doc, rows):
    cells = [[c.strip() for c in re.split(r"(?<!\\)\|", r.strip().strip("|"))] for r in rows]
    cells = [r for r in cells if not all(re.fullmatch(r":?-{3,}:?", c) for c in r)]
    table = doc.add_table(rows=len(cells), cols=len(cells[0]))
    table.style = "Table Grid"
    for i, row in enumerate(cells):
        # Keep the table on one page: rows don't split, each row sticks to the next.
        tr_pr = table.rows[i]._tr.get_or_add_trPr()
        tr_pr.append(OxmlElement("w:cantSplit"))
        for j, text in enumerate(row):
            par = table.cell(i, j).paragraphs[0]
            fmt(par, after=0, line=1.0, keep=(i < len(cells) - 1))
            add_inline(par, text, 11, bold=(i == 0), code_size=10)
    fmt(doc.add_paragraph(), after=0)


def body(doc, lines):
    i = 0
    while i < len(lines):
        line = lines[i]
        if line.startswith("## "):
            h = doc.add_heading(level=1)
            add_inline(h, line[3:], 16, bold=True)
            i += 1
        elif line.startswith("### "):
            h = doc.add_heading(level=2)
            add_inline(h, line[4:], 14, bold=True, code_size=12)
            i += 1
        elif line.startswith("```"):
            j = i + 1
            while not lines[j].startswith("```"):
                j += 1
            p = fmt(doc.add_paragraph(), before=2, after=8, indent=0.75, first=0, line=1.0)
            run = p.add_run("\n".join(lines[i + 1:j]))
            set_font(run, MONO, CODE_BLOCK)
            no_proof(run)
            i = j + 1
        elif line.startswith("|"):
            j = i
            while j < len(lines) and lines[j].startswith("|"):
                j += 1
            add_table(doc, lines[i:j])
            i = j
        elif SIGNATURE_ITEM.match(line):
            sig, text = SIGNATURE_ITEM.match(line).groups()
            p = fmt(doc.add_paragraph(), before=6, after=0, first=0, keep=True, line=1.0)
            add_inline(p, sig, BODY, code_size=CODE_INLINE)
            p = fmt(doc.add_paragraph(), after=4, indent=1.0, first=0)
            add_inline(p, text, BODY, code_size=CODE_INLINE)
            i += 1
        elif line.startswith("- "):
            p = fmt(doc.add_paragraph(style="List Bullet"), after=3)
            add_inline(p, line[2:], code_size=CODE_INLINE)
            i += 1
        elif re.match(r"^\d+\. ", line):
            p = fmt(doc.add_paragraph(style="List Number"), after=3)
            add_inline(p, re.sub(r"^\d+\. ", "", line), code_size=CODE_INLINE)
            i += 1
        elif line.startswith("> "):
            p = fmt(doc.add_paragraph(), indent=1.0, first=0)
            add_inline(p, line[2:], italic=True, code_size=CODE_INLINE)
            i += 1
        elif line.strip() == "":
            i += 1
        else:
            p = fmt(doc.add_paragraph(), first=1.0)
            # Justify plain text only: long `code` words make huge gaps when stretched.
            if "`" not in line:
                p.alignment = WD_ALIGN_PARAGRAPH.JUSTIFY
            add_inline(p, line, code_size=CODE_INLINE)
            i += 1


def build(lines, pages, dst):
    first = next(i for i, l in enumerate(lines) if l.startswith("## "))
    blocks, cur = [], []
    for l in lines[1:first]:
        if l.strip():
            cur.append(l.strip())
        elif cur:
            blocks.append(cur)
            cur = []
    if cur:
        blocks.append(cur)
    headings = [l[3:] for l in lines[first:] if l.startswith("## ")]

    doc = Document()
    setup(doc)
    footers(doc, blocks[-1][0])
    title_page(doc, blocks)
    contents(doc, headings, pages)
    body(doc, lines[first:])
    doc.save(dst)
    return headings


# ---------- page numbers from a test render ----------

def render_pdf(docx, pdf):
    """Renders docx to PDF with the OnlyOffice converter. False if unavailable."""
    if not (os.path.exists(X2T) and os.path.exists(os.path.join(OO_FONTS, "AllFonts.js"))):
        return False
    tmp = tempfile.mkdtemp()
    try:
        cfg = os.path.join(tmp, "task.xml")
        with open(cfg, "w", encoding="utf-8") as f:
            f.write(f"""<?xml version="1.0" encoding="utf-8"?>
<TaskQueueDataConvert><m_sFileFrom>{os.path.abspath(docx)}</m_sFileFrom>
<m_sFileTo>{os.path.abspath(pdf)}</m_sFileTo><m_nFormatTo>513</m_nFormatTo>
<m_sAllFontsPath>{OO_FONTS}/AllFonts.js</m_sAllFontsPath><m_sFontDir>{OO_FONTS}</m_sFontDir>
<m_sTempDir>{tmp}</m_sTempDir></TaskQueueDataConvert>""")
        subprocess.run([X2T, cfg], capture_output=True, timeout=180)
        return os.path.exists(pdf)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def heading_pages(docx, headings):
    """Page number of every "## " heading, from a test render."""
    if not shutil.which("pdftotext"):
        return {}
    tmp = tempfile.mkdtemp()
    try:
        pdf = os.path.join(tmp, "out.pdf")
        if not render_pdf(docx, pdf):
            return {}
        text = subprocess.run(["pdftotext", pdf, "-"], capture_output=True, text=True).stdout
        pages = text.split("\f")
        found, start = {}, 2   # page 1 = title, page 2 = contents
        for h in headings:
            for n in range(start, len(pages)):
                if any(l.strip() == h for l in pages[n].splitlines()):
                    found[h] = n + 1
                    start = n
                    break
        return found
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main(src, dst):
    lines = open(src, encoding="utf-8").read().splitlines()
    # Pass 1 without numbers, pass 2 with the numbers found in the render of pass 1.
    # The contents has one line per heading either way, so the layout does not move.
    headings = build(lines, {}, dst)
    pages = heading_pages(dst, headings)
    if pages:
        build(lines, pages, dst)
        print(f"written {dst}, contents: {pages}")
    else:
        print(f"written {dst}; no renderer found, update the contents in the editor (F9)")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
