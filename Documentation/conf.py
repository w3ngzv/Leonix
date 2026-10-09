# SPDX-License-Identifier: GPL-2.0-only
#
# Sphinx configuration for the Leonix documentation.  Built with
# "make htmldocs" from the top of the tree.

import os

project = "Leonix"
copyright = "2026, KernelKraze"
language = "zh_CN"

root_doc = "index"

# Generated files, and the datasheets kept out of the tree by
# .git/info/exclude, are not sources.
exclude_patterns = ["output", "sphinx", "**/*.pdf", "**/*.html"]

# Same scheme as Linux: alabaster unless DOCS_THEME names another theme.
html_theme = os.environ.get("DOCS_THEME", "alabaster")

html_title = "Leonix"
html_show_sourcelink = False


# The sources are wrapped at a fixed width, so a line often breaks
# between two Chinese characters.  HTML turns that newline into a space,
# which Chinese text does not have.  Join such lines after parsing; code
# and literal blocks keep their newlines.
import re

from docutils import nodes

_CJK = "　-〿一-鿿＀-￯"
_CJK_BREAK = re.compile(r"(?<=[%s])\n[ \t]*(?=[%s])" % (_CJK, _CJK))


def _join_cjk_lines(app, doctree):
    for text in list(doctree.findall(nodes.Text)):
        parent = text.parent
        if isinstance(parent, (nodes.literal_block, nodes.literal, nodes.raw)):
            continue
        joined = _CJK_BREAK.sub("", text.astext())
        if joined != text.astext():
            parent.replace(text, nodes.Text(joined))


# Sphinx 9.1.0 ships the English stemmer for Chinese search but then
# emits "window.Stemmer = ChineseStemmer", a class no file defines, and
# the search page stops with a ReferenceError.  The subclass only renames
# the stemmer to the one that is actually loaded.
from sphinx.search.zh import SearchChinese


class _SearchChinese(SearchChinese):
    language_name = "English"


def setup(app):
    app.connect("doctree-read", _join_cjk_lines)
    app.add_search_language(_SearchChinese)
