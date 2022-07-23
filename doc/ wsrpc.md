#WSRPC

Nárvh formátu pro obecný přenos dat a zprát protokolem WS za použití JSONu

## Požadované featury

* textový i binární přenos dat
* textový formát JSON - řízení přenosu
* binární formát "any" - payload (volitelné)
* request-response = oboustranně možný
* publisher-subscriber
* routing (optional)

## Formát

```
["Txxxxxxx",<payload>]
```

T - Typ zprávy
xxxxx - ID zprávy

* **H** - Hello, první zpráva, kterou by měl poslat "client", tedy ten který vytvořil spojení. ID je volitelné
* **W** - Welcome, odpověď na **H**, posílá "server"
* **C** - Call, volání RPC, ID si volí volající a musí být unikátní, ale může obsahovat cokoliv. (`C12345`, `Caabbcc`)
* **R** - Result, výsledek volání RPC, ID musí být stejné jako pro C, aby bylo možné odpověď spárovat
* **E** - Error/Exception
* **?** - Neznámé volání.
* **T** - Topic, posílá publisher, ID obsahuje název topicu a viz dále
* **U** - Unsubscribe, posílá subscriber, když se chce odhlásit, uvádí ID = topic. Přihlášení se dělá přes C/R
* **N** - End of topic, unsubscribe ze strany publishera - oznamuje, ze vsechny zprávy zdaného topicu byly odeslány a nic dalšího už nepřijde
* **S** - Set - nastavení proměnné spojení (Snazev)



Zprávy `H` a `W` jsou jediné zprávy, které odlišují jednotlivé strany podle toho, kdo otevřel spojení a kdo ho akceptoval. Ostatní zprávy musí umět zpracovat obě strany bez ohledu kdo navazoval spojení. Ten který otevřel spojení se tak může ve výsledku chovat jako server a naopak

### Zpráva H - Hello

```
["H","version",<payload/authorization>]
```
* **version** - vždy 1.0.0
* **payload/authorization** - volitelný obsah, který se předává na druhou stranu, může obsahovat formu autorizace. 

### Zpráva W - Welcome

```
["W","version",<optional>]
```
* **version** - vždy 1.0.0
* **optional** - volitelný nepovinný obsah


### Zpráva C - Call
Zpráva představuje volání vzdálené funkce

```
["Cxxxxx","method",[...params...]]
```

* **xxxxx** ID zprávy, měl by být unikátní ale obsah řetězce je plně volitelný. Doporučení: řetězec by neměl mít víc jako 100 znaků, ale není to pevný limit.
* **method** Jméno funkce, metody, která je volána
* **params** Parametry funkce. Může být posláno i jako objekt

Druhá strana musí na volání odpovědět, i kdyby volání nebylo podporováno

### Zpráva R - Result

```
["Rxxxxx",<data>]
```

* **xxxxx** ID zprávy na kterou se odpovídá
* **data** - data výsledku

### Zpráva E - Exception

```
["E","Connection exception"]
["Exxxxx","Method exception"]
```

Pokud není uvedeno ID, pak se jedná o výjimku výzanou na spojení a zpravidla se jedná o poslední zprávu před ukončením spojení

Pokud je uvedeno ID, pak jde o výjimku při zpracování `C`. Nahrazuje to odpověď na dané volání

### Zpráva ? - Not found

```
["?xxxxx","Method not found"]
```
Objevuje se při pokusu zavolat (`C`) metodu, která neexistuje, případně nebyla nalezena cesta k danému prostředku


### Zpráva T - Topic

Pokud je jedna strana přihlášena k odběru, pak druhá strana zasílá zprávy z odběru pro daný topic. Jméno topicu si obě strany vyjednají pomocí RPC. Tedy neexistuje přihlašovací zpráva. 

```
["Txxxxxx", <data>]
```

* **xxxxxx** - ID topicu

Číslování zpráv topicu je plně v režii aplikace, včetně určení, jak budou zprávy číslovány i jakým způsobem bude obnovování ukončeného spojení - 

### Zpráva U - Unsubscribe

Umožňuje odhlásit topic ze strany subscribera. Posálá se publisherovi. 

```
["Uxxxxxx"]
```

* **xxxxxx** - ID topicu k odhlášení
 
### Zpráva N - End of topic

Oznamuje subscriberovi, že veškerý topic byl vyčerpán, žádná další zpráva již nepřijde


### Zpráva S - Set

Každá strana si může evidovat proměnné specifické pro dané spojení. Příkladem může být autorizace, atd. Jedna strana může mít podmínku nastavení nějaké proměnné druhou stranou, než je komunikace povolena

```
["Sname",<data>]
```
* **name** - jméno proměnné
* **data** - data, pokud položka chybí, pak jde o smazání proměnné

```
["Stoken","apoikdpowjed0jd20jdioedcxwid20"]


["Stoken"]
```

Druhá strana nastavení proměnné nepotvrzuje. Pokud proměnná není podporována, je požadavek zahozen

##Routování 

Jméno metody může obsahovat i cestu k cíli "node1:node2:function"

## Komunikační patterny

### Request-Response

Základní komunikační pattern pomocí zpráv "C" a "R". Je povoleno posílat víc požadavků současně, musí však být zaručen příjem a zpracování odpovědí též současně. ID zprávy v tomto případě identifikuje ID volání a ID odpovědi, tak aby bylo možné odpověď přiřadit patřičnému volání


### Subscriber-Publisher

Přihlášení (subscribe) k odběru je třeba vyjednat přes Request-Response. Součástí odpovědi pak může být i ID topicu, který je následně zasílán do otevřeného spojení. 

Odhlášení lze provést přes "Utopic". Není třeba výtvářet volání pro odhlášení. Tato zpráva je zavedena také k vůli možnosti odhlásit nevyžádaný topic, nebo topic zasílaný v důsledku chyby. Subscriber musí daný topic odhlásit.

Pokud se daný topic  vyčerpal, zasle se místo zprávy T, zpráva N s id topicu

```
["C1","subscribe",[]]

["R1",{"topic":"1234"}]
["T1234",...data...]
["T1234",...data...]
["T1234",...data...]
["T1234",...data...]
["T1234",...data...]
["N1234"]
```

### Pull - Push

Push pull lze řešit pomocí Requet/Response s tím, že při PULL je requestem z jedné strany a PUSH requestem z druhé strany

```
pulling side       pushing side

["C1","ready"]
                   ["R1"]
                
                   ["C2","get_work"]
["R2","working"]
```


## Binarní data

Binární data nemají žádný specifický formát a jsou posílány binárním frame. Identifikaci těchto dat se provádí pomocí HASH (SHA1) v Base64. 

Binární data je potřeba ohlásít dopředu, například pomocí Call

```
["C1","put_file","data.txt",["hash1","hash2","hash3",....]]
<binary frame hash1>
<binary frame hash2>
<binary frame hash3>
                                 [R1,true]
```

Server na základě přijetí zprávy C1 registruje uvedené hashe pro příjem. Pokud je přijat
patřičný binarní frame, pak je předán otevřenému requestu, zároveň je hash smazán. 
(1 hash může být registrován vícekrát, pak se ale musí poslat vícekrát stejný úsek dat - nicméně může existovat domluva, že více stejných hashů v rámci jedné metody nebude znamenat vícenásobnou registraci).

Binární framy je doporučeno posílat v dohodnutém pořadí

Volba formátu hashe může být určen prefixem

```
md5:xxxxxxx
sha1:xxxxxx
ripemd160:xxxxxxx
sha256:xxxxxxxx
```
Hash bez identifikace je `sha1`



Binární data lze zasílat i zprávou **Topic** nebo i **Response**. 

```
["Tchanges","data.txt",["hash1","hash2","hash3",....]]
<binary frame hash1>
<binary frame hash2>
<binary frame hash3>
```


Implementace: Oba uzly mohou registrovat hash a callback, který se zavolá, pokud je přijat
binární frame s odpovídajícím hashem. Pokud uzel přijme binární frame, spočítá hash a pokud najde registraci, je zavolán patřičný callback a registrace je smazána. Pokud je přijat binární frame který není registrován, pak je zahozen.

Sekvence binárních framů nemusí být souvislá. Jiné framy jsou povoleny. Pokud se přenáší víc binárních dat současně, mohou se jejich framy střídat

