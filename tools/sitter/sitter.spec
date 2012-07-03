# -*- mode: python -*-
a = Analysis([os.path.join(HOMEPATH,'support\\_mountzlib.py'), os.path.join(HOMEPATH,'support\\unpackTK.py'), os.path.join(HOMEPATH,'support\\useTK.py'), os.path.join(HOMEPATH,'support\\useUnicode.py'), 'sitter.py', os.path.join(HOMEPATH,'support\\removeTK.py')],
             pathex=['Z:\\j4cDAC\\tools\\sitter'])
pyz = PYZ(a.pure)
exe = EXE(TkPKG(), pyz,
          a.scripts,
          a.binaries,
          a.zipfiles,
          a.datas,
          name=os.path.join('dist', 'sitter.exe'),
          debug=False,
          strip=False,
          upx=True,
          console=False )
app = BUNDLE(exe,
             name=os.path.join('dist', 'sitter.exe.app'))
