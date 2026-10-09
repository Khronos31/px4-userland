import json,glob,os,sys,collections
root=sys.argv[1]
runs=[]
for d in sorted(glob.glob(os.path.join(root,'*'))):
    fn=os.path.join(d,'probe.json')
    if not os.path.isfile(fn): continue
    cond=os.path.basename(d).split('-',1)[1]
    j=json.load(open(fn))
    runs.append((os.path.basename(d),cond,j))
agg=collections.OrderedDict()
detail=[]
for name,cond,j in runs:
    a=agg.setdefault(cond,dict(runs=0,droprun=0,lost=0,ccerr=0,rx=collections.Counter(),burst=collections.Counter(),burstruns=0,simul=[],tei=0,sync=0,bad=0, deliv=0))
    a['runs']+=1
    rxs=j['rx']
    bad=[r for r in rxs if not r['tune_ok'] and j['mode']=='bon']
    if j['status']!='ok' or bad: a['bad']+=1
    burst=[r for r in rxs if r['tei']>1000]
    if burst: a['burstruns']+=1
    for r in burst: a['burst'][r['label']]+=1
    nb=[r for r in rxs if r['tei']<=1000]
    drops=[r for r in nb if r['cc_errors']>0]
    for r in nb: a['tei']+=r['tei']; a['sync']+=r['sync_loss']; a['deliv']+=r['packets_delivered']
    if drops:
        a['droprun']+=1
        evs=[]
        for r in drops:
            a['rx'][r['label']]+=1; a['lost']+=r['est_lost']; a['ccerr']+=r['cc_errors']
            for e in r['events']:
                if not e[6]: evs.append((e[0],r['label']))
        evs.sort()
        # cluster events within 200 ms
        cl=[]; 
        for t,l in evs:
            if cl and t-cl[-1][-1][0]<=200: cl[-1].append((t,l))
            else: cl.append([(t,l)])
        desc=[f"{c[0][0]/1000:.2f}s[{'+'.join(sorted(set(x[1] for x in c)))}]" for c in cl]
        a['simul'].append(f"{name}: "+' '.join(desc))
        detail.append((name,cond,[(r['label'],r['cc_errors'],r['est_lost']) for r in drops],desc))
    if j['mode']=='px4ts':
        for r in rxs:
            if r['exit_code']!=0 and r['tei']<=1000: pass
for c,a in agg.items():
    print(f"== {c}: runs={a['runs']} invalid={a['bad']} drop-runs(non-burst)={a['droprun']} cc-errors={a['ccerr']} est-lost-pk={a['lost']} rx={dict(a['rx'])} burst-runs={a['burstruns']} burst-rx={dict(a['burst'])} nonburst tei={a['tei']} sync={a['sync']} delivered={a['deliv']}")
    for s in a['simul']: print("   ",s)
