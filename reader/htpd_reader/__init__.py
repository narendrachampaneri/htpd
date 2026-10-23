"""htpd_reader - the HTPD reader: read bundles and find the text that answers a question.

    from htpd_reader import open_bundle
    b = open_bundle("manual.htpd")
    for node, depth in b.walk():
        print("  " * depth + node.title, node.page_start)

The format it reads is docs/FORMAT.md.
"""

__version__ = "0.1.2.post1"

from .bundle import Block, Bundle, BundleError, Node, Problem, open_bundle  # noqa: E402
from .check import check  # noqa: E402
from .search import search  # noqa: E402

__all__ = ["Block", "Bundle", "BundleError", "Node", "Problem", "open_bundle", "check", "search", "__version__"]
