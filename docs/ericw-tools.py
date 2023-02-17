from sphinx.application import Sphinx
from sphinx import addnodes
import re

keyvalue_regex = re.compile(r'"([^"]+)" "([^"]+)"')
'''
given '"abc" "def"' extracts abc, def as two capture groups
'''

def parse_epair(env, sig: str, signode) -> str:
    '''
    parses a directive signature like::
    
        .. worldspawn-key: "light" "n"
        
            description here
        
    and returns 'light' as the object name (for use in the help index).
    '''
    m = keyvalue_regex.match(sig)
 
    if not m:
         signode += addnodes.desc_name(sig, sig)
         return sig
    
    name, args = m.groups()
        
    signode += addnodes.desc_name(name, name)
    signode += addnodes.desc_sig_literal_string(text=' ')    
    signode += addnodes.desc_sig_literal_string(text=args)

    return name


def setup(app: Sphinx):
    app.add_object_type(
            directivename='worldspawn-key',
            rolename='worldspawn-key',
            indextemplate='pair: %s; worldspawn key',
            parse_node=parse_epair,
        )
    app.add_object_type(
            directivename='bmodel-key',
            rolename='bmodel-key',
            indextemplate='pair: %s; bmodel key',
            parse_node=parse_epair,
        )
    app.add_object_type(
            directivename='light-key',
            rolename='light-key',
            indextemplate='pair: %s; light entity key',
            parse_node=parse_epair,
        )
    app.add_object_type(
            directivename='classname',
            rolename='classname',
            indextemplate='pair: %s; classname',
        )
    app.add_object_type(
            directivename='other-key',
            rolename='other-key',
            indextemplate='pair: %s; other entity key',
        )
    app.add_object_type(
            directivename='texture',
            rolename='texture',
            indextemplate='pair: %s; texture name',
        )
