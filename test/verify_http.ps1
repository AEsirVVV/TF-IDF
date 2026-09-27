# verify_http.ps1 —— 对运行中的 HTTP 服务做端到端查询正确性断言。
#
# 用法：
#   1. 先启动服务：mingw32-make run   （或直接运行 output\main.exe）
#   2. 再运行本脚本：pwsh test\verify_http.ps1
#      （Windows PowerShell 5.1 亦可：powershell -File test\verify_http.ps1）
#
# 验证内容（共 45 条，与 test_verify.cpp 的断言表保持一致）：
#   - 原始题材金标准：20 个主题明确的查询，断言"预期文档必须排第一名"；
#   - 扩充语料新题材金标准：20 个新题材查询（80 个新题材中抽样），同样断言 top-1；
#   - 边界：纯停用词 / 空查询 / 不存在的词 / 语料中未出现的英文实词 → 必须返回空数组；
#   - 通用不变量：分数在 [0,1]、结果按相似度降序、数量 ≤ 5。
#
# 退出码：0 = 全部通过；1 = 存在失败（可在 CI/脚本中直接使用）。

param(
    [string]$Base = "http://localhost:8080"
)

# 金标准测试表：q = 查询词，top = 预期第一名；empty = true 表示必须无结果
$tests = @(
    # —— 原始 19 篇语料的 13 个题材 ——
    @{ q = "basketball";       top = "data3.txt"  },
    @{ q = "football";         top = "data9.txt"  },
    @{ q = "health";           top = "data7.txt"  },
    @{ q = "climate change";   top = "data2.txt"  },
    @{ q = "music";            top = "data8.txt"  },
    @{ q = "ancient rome";     top = "data4.txt"  },
    @{ q = "storytelling";     top = "data5.txt"  },
    @{ q = "programming";      top = "data13.txt" },
    @{ q = "economy";          top = "data14.txt" },
    @{ q = "space";            top = "data15.txt" },
    @{ q = "AI";               top = "data1.txt"  },
    @{ q = "machine learning"; top = "data13.txt" },
    @{ q = "足球";             top = "data11.txt" },
    @{ q = "人工智能";          top = "data6.txt"  },
    @{ q = "机器学习";          top = "data10.txt" },
    @{ q = "健康";             top = "data12.txt" },
    @{ q = "太空";             top = "data19.txt" },
    @{ q = "美食";             top = "data18.txt" },
    @{ q = "教育";             top = "data16.txt" },
    @{ q = "编程";             top = "data10.txt" },
    # —— 扩充语料（data20 ~ data500）的新题材 ——
    @{ q = "茶文化";           top = "data100.txt" },
    @{ q = "围棋";             top = "data344.txt" },
    @{ q = "咖啡";             top = "data190.txt" },
    @{ q = "鸟类";             top = "data124.txt" },
    @{ q = "钓鱼";             top = "data76.txt"  },
    @{ q = "香料";             top = "data80.txt"  },
    @{ q = "灯笼";             top = "data254.txt" },
    @{ q = "漆艺";             top = "data258.txt" },
    @{ q = "早市";             top = "data242.txt" },
    @{ q = "旧书";             top = "data486.txt" },
    @{ q = "瑜伽";             top = "data390.txt" },
    @{ q = "山脉";             top = "data38.txt"  },
    @{ q = "starter";          top = "data421.txt" },
    @{ q = "espresso";         top = "data103.txt" },
    @{ q = "barometer";        top = "data389.txt" },
    @{ q = "quoin";            top = "data47.txt"  },
    @{ q = "escapement";       top = "data99.txt"  },
    @{ q = "joinery";          top = "data347.txt" },
    @{ q = "slate";            top = "data485.txt" },
    @{ q = "willow";           top = "data491.txt" },
    # —— 边界情况 ——
    @{ q = "the";              empty = $true      },
    @{ q = "";                 empty = $true      },
    @{ q = "zzzqqq";           empty = $true      },
    @{ q = "apiary";           empty = $true      },   # 语料中确实未出现的英文实词
    @{ q = "kayak";            empty = $true      }
)

$pass = 0
$fail = 0

foreach ($t in $tests) {
    $isEmptyExpect = ($t.ContainsKey("empty") -and $t.empty)
    try {
        $url = "$Base/search?q=" + [uri]::EscapeDataString($t.q)
        $resp = Invoke-WebRequest -Uri $url -UseBasicParsing -TimeoutSec 10

        # 解析 JSON（空数组 [] 在 PowerShell 里解析为 $null，统一转成数组处理）
        $data = $resp.Content | ConvertFrom-Json
        if ($null -eq $data) {
            $count = 0
            $top = ""
        } else {
            $arr = @($data)
            $count = $arr.Count
            $top = if ($count -gt 0) { $arr[0].doc_name } else { "" }
        }

        # 通用不变量：分数范围 [0,1]、降序、数量 <= 5
        $invariantOk = $true
        $lastScore = [double]::MaxValue
        if ($null -ne $data) {
            foreach ($item in @($data)) {
                if ($item.score -lt 0.0 -or $item.score -gt 1.0) { $invariantOk = $false }
                if ($item.score -gt $lastScore) { $invariantOk = $false }
                $lastScore = [double]$item.score
            }
            if (@($data).Count -gt 5) { $invariantOk = $false }
        }

        # 金标准断言
        if ($isEmptyExpect) {
            $expectOk = ($count -eq 0)
        } else {
            $expectOk = ($count -gt 0 -and $top -eq $t.top)
        }

        if ($expectOk -and $invariantOk) {
            $pass++
            $label = if ($isEmptyExpect) { "(空)" } else { $top }
            Write-Host ("PASS  [{0}] -> {1}" -f $t.q, $label)
        } else {
            $fail++
            $want = if ($isEmptyExpect) { "(空)" } else { $t.top }
            Write-Host ("FAIL  [{0}] 期望 {1} | 实际: {2}" -f $t.q, $want, $resp.Content)
            if (-not $invariantOk) { Write-Host "      [不变量破坏: 分数范围/降序/数量]" }
        }
    } catch {
        $fail++
        Write-Host ("FAIL  [{0}] 请求出错: {1}" -f $t.q, $_.Exception.Message)
    }
}

Write-Host "========================================"
Write-Host ("通过 {0} 条，失败 {1} 条" -f $pass, $fail)
if ($fail -eq 0) {
    Write-Host "全部通过 —— 端到端查询结果正确性得到验证"
    exit 0
} else {
    Write-Host "存在失败，请检查上述 FAIL 项"
    exit 1
}
