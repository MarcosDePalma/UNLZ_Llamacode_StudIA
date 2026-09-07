$ErrorActionPreference = 'Stop'
$server='D:\Models\llamacpp\llama.cpp-b10331-cuda12.4\llama-server.exe'
$model='D:\Models\llamacpp\Qwen3.8-27B-GGUF-Q4_K_M\Qwen3.8-27B-Q4_K_M.gguf'
$mm='D:\Models\llamacpp\Qwen3.8-27B-GGUF-Q4_K_M\mmproj-BF16.gguf'
$tpl=(Resolve-Path 'assets\chat-templates\qwen38-tools-fixed.jinja').Path
$port=18090
$tasks=@(
 @{id='last-digit'; expected='(?im)FINAL:\s*3\b'; prompt='Compute the last digit of 7 to the power of 7 to the power of 7. Explain briefly and end with FINAL: 3.'},
 @{id='mississippi'; expected='(?im)FINAL:\s*34650\b'; prompt='How many distinct permutations does MISSISSIPPI have? Explain briefly and end with FINAL: 34650.'},
 @{id='telescoping'; expected='(?im)FINAL:\s*100/101\b'; prompt='Compute sum k=1..100 of 1/(k(k+1)). Explain briefly and end with FINAL: 100/101.'},
 @{id='modular'; expected='(?im)FINAL:\s*1\b'; prompt='Compute 2 to the power 100 modulo 125. Explain briefly and end with FINAL: 1.'},
 @{id='logic'; expected='(?im)FINAL:\s*NO\b'; prompt='All ravens are birds. Some birds cannot fly. Does it follow that some ravens cannot fly? Explain briefly and end with FINAL: NO.'},
 @{id='python-output'; expected='(?im)FINAL:\s*\[1,4,9\]'; prompt='What does Python print for [x*x for x in range(1,4)]? End with FINAL: [1,4,9].'}
)
$log=Join-Path ([IO.Path]::GetTempPath()) 'qwen38-template-quality'
$a=@('-m',$model,'--mmproj',$mm,'--chat-template-file',$tpl,'--host','127.0.0.1','--port',"$port",'--ctx-size','65536','--parallel','1','--n-gpu-layers','999','--split-mode','layer','--tensor-split','1,1','--cache-type-k','q8_0','--cache-type-v','q8_0','--flash-attn','on','--batch-size','2048','--ubatch-size','512','--spec-type','draft-mtp','--spec-draft-n-max','4','--temp','0.6','--top-p','0.95','--top-k','20','--min-p','0.0','--reasoning','off','--jinja','--metrics','--no-warmup')
$p=Start-Process $server -ArgumentList $a -RedirectStandardOutput ($log+'.out.log') -RedirectStandardError ($log+'.err.log') -PassThru -WindowStyle Hidden
try {
 $healthy=$false; for($i=0;$i -lt 90;$i++){try{$h=Invoke-RestMethod "http://127.0.0.1:$port/health" -TimeoutSec 2;if($h.status -eq 'ok'){$healthy=$true;break}}catch{};Start-Sleep 2}; if(!$healthy){throw 'server not healthy'}
 $rows=@(); foreach($pass in 1..2){foreach($t in $tasks){$body=@{messages=@(@{role='user';content=$t.prompt});max_tokens=768;temperature=.6;top_p=.95;stream=$false;chat_template_kwargs=@{enable_thinking=$false;preserve_thinking=$true;reasoning_effort='medium'}}|ConvertTo-Json -Depth 10;$r=Invoke-RestMethod "http://127.0.0.1:$port/v1/chat/completions" -Method Post -ContentType 'application/json' -Body $body -TimeoutSec 180;$m=$r.choices[0].message;$content=(($m.content,$m.reasoning_content)-join "`n").Trim();$rows += [ordered]@{pass=$pass;task=$t.id;ok=[regex]::IsMatch($content,$t.expected);tokens=[int]$r.timings.predicted_n;content=$content}}}
 $result=[ordered]@{template='qwen3.8-safe-v2';model=$model;mtp=4;thinking='off';runs=$rows;score=("{0}/{1}" -f @($rows|? ok).Count,$rows.Count)}; $result|ConvertTo-Json -Depth 12|Set-Content 'docs\qwen38-template-quality-20260814.json' -Encoding utf8; $result.score
} finally {if(!$p.HasExited){Stop-Process $p.Id -Force};$p.WaitForExit()}
