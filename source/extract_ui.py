"""Extract visible strings from TopoGun UI definitions without changing them."""
import json
import re
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DATA = Path.home() / 'AppData/Roaming/TopoGun3'
fields = {'text', 'title', 'windowTitle', 'toolTip', 'statusTip', 'whatsThis', 'placeholderText', 'buttonText', 'suffix', 'prefix'}
terms = {}
errors = []
for folder in ('ui', 'tools', 'menubar', 'toolbars', 'piemenu', 'shortcuts'):
    for path in sorted((DATA / folder).rglob('*')):
        if path.suffix not in ('.ui', '.xml'):
            continue
        try:
            content = re.sub(r'<\?xml[^>]*\?>', '', path.read_text(encoding='utf-8-sig'))
            tree = ET.fromstring('<root>' + content + '</root>')
            found = []
            for node in tree.iter():
                found += [value for key, value in node.attrib.items() if key in fields]
                if node.tag == 'property' and node.get('name') in fields:
                    found += [n.text or '' for n in node.findall('string') if n.get('notr') != 'true']
            for text in found:
                if not text.strip() or not re.search('[A-Za-z]', text):
                    continue
                if text in {'Form', 'Dialog', 'TextLabel', 'MainWindow'}:
                    continue
                terms.setdefault(text, []).append(str(path.relative_to(DATA)))
        except Exception as error:
            errors.append({'file': str(path), 'error': str(error)})
translated = {}
for path in sorted((ROOT / 'source/translations').glob('*_zh.json')):
    translated.update(json.loads(path.read_text(encoding='utf-8-sig'))['translations'])
missing = {k: v for k, v in sorted(terms.items()) if not translated.get(k)}
out = ROOT / 'build/extraction'
out.mkdir(parents=True, exist_ok=True)
for name, data in [('ui_sources.json', terms), ('ui_missing.json', missing), ('ui_errors.json', errors)]:
    (out / name).write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps({'ui_unique': len(terms), 'missing': len(missing), 'parse_errors': len(errors)}))
