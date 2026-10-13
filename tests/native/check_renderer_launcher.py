#!/usr/bin/env python3
"""Verify argument handling, explicit failure and process status without GUI startup."""
import json, os, pathlib, subprocess, tempfile
root=pathlib.Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='xara-launcher-test.') as tmp:
    tmp=pathlib.Path(tmp); stub=tmp/'record-args'; output=tmp/'args.json'
    stub.write_text('#!/usr/bin/env python3\nimport json,os,sys\nopen(os.environ["TEST_ARG_FILE"],"w").write(json.dumps(sys.argv[1:]))\nsys.exit(23)\n')
    stub.chmod(0o700)
    env=dict(os.environ,XARA_CDRAW_BINARY=str(stub),TEST_ARG_FILE=str(output))
    for args,want in [(['a file.xar'],['a file.xar']),
                     (['--renderer=cdraw','--help'],['--help']),
                     (['--renderer','cdraw','--','--renderer=native'],['--','--renderer=native'])]:
        r=subprocess.run([str(root/'scripts/xara-vengeance'),*args],env=env,cwd=tmp,capture_output=True)
        assert r.returncode==23,(args,r.stderr)
        assert json.loads(output.read_text())==want
    recorded=output.read_bytes()
    for args,status in [(['--renderer=native'],69),(['--renderer=other'],64),
                        (['--renderer=cdraw','--renderer=native'],64),(['--renderer'],64)]:
        r=subprocess.run([str(root/'scripts/xara-vengeance'),*args],env=env,capture_output=True)
        assert r.returncode==status,(args,r.stderr)
        assert output.read_bytes()==recorded, 'Rejected backend unexpectedly launched a process'
print('PASS: default/explicit CDraw, argument boundaries, status propagation and native rejection')
