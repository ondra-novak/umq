# UMQ 

UMQ je jednoduchý message and queueing system a protokol 

## Features

* podpora websockets
* asynchroní řízení
* jedno spojení podporuje všechny možnosti komunikace (virtuální kanály)
* request-response
* publisher-subscriber
* push-pull
* callbacky
* sdílené proměnné
* binární data

## Role stran

Při zahájení komunikace jedna strana je server a druhá klient. Po dokončení handshake však toto rozdělení končí, obě strany se nazývají peer a nemají žádnou přidělenou roli, tzn obě strany se mohou chovat jako server tak klient. 

Tzn je možné, aby se klient (peer který se připojí na server) začal chovat jako server a nabízet služby.

## Popis protokolu

Protokol používá websocket textové rámce. Pokud je protokol použit mimo systém websockety, vyžaduje implementaci rámců a jejich rozdělení na na textové a binární.

Textové rámce vyžadují kódování UTF-8 (vyžadování websocket standardem, mimo tento standard není kódování vyžadováno)

UMQ rámec má jednoduchou strkturu

```
   1b                 vary            1b               vary
+------+------+------+--//---+------+------+------+------+------+------+ ...
| type |            identifier      |  \n  |     ... payload ...
+------+------+------+--//---+------+------+------+------+------+------+ ...

```

* **type** - jeden znak definuje typ zprávy
* **identifier** - identifikátor jeho význam záleží na typu zprávy. Typicky identifikuje komunikační kanál, avšak může být využit jinak u zpráv, které nejsou součástí komunikačních kanálů. Identifikátor je libovolný UTF-8 string, nesmí být prázdný, a nesmí obsahovat znak \n (new line), protože to je oddělovač
* **payload** - obsah zprávy, může být vynechán, pak není potřeba uvádět ani oddělovač.

## Typy zpráv

* **!** - Execution error
* **?** - Method help
* **C** - Callback call
* **E** - Exception
* **H** - Hello message
* **M** - Method call
* **R** - Result
* **S** - Var set
* **T** - Topic update
* **U** - Unsubscribe
* **W** - Welcome message
* **X** - Var unset
* **Z** - Topic close


## Komunikační patterny

### Počáteční handshake

Ihned po navázání spojení je třeba provést počáteční handshake s protistranou. Tento hanshake zahajuje klient - ten který se připojil na otevřený port - a dokončuje ho server

```
client: "H<version>\n<data>"
server: "W<version>\n<data>"
```

Klient posílá zprávu **Hello (H)** a jako identifikátor zprávy se udává verze protokolu, která je v současné době 1.0.0. Server zkontroluje verzi a odpoví zprávou **Welcome (W)**, předává číslo verze, kterou chce používat. Obě zprávy mohou obsahovat libovolná data které mohou zpracovat obě strany dle svého uvážení.

### Globální chybové stavy (+chyba při handshake)

Pokud handshake selže, odesílá kterákoliv strana zprávu **Exception (E)**

```
"E\nerror descriptin"
```

Zpráve **E** v tomto případě obsahuje prázdný identifikátor. Typicky ihned po odeslání této zprávy zároveň odesílatel zavírá spojení.

Globální chyba **E** se může objevit během komunikace a je vždy doprovázena zavřením spojení.

### Request-Response

Request zpravidla oslovuje nějakou methodu na protější straně, která request očekává - je třeba druhé straně říct, co requestem je požadováno. Jedná se tedy o volání vzdálené procedury (RPC). 

Request se zahajuje zasláním zprávy **Method call (M)**

```
M<id>\n<name>\n<data>
```

Tato zpráva má navíc v payloadu jméno metody/procedury, která se má na druhé straně spojení zavolat. Metodě se předají parametry `<data>`. Identifikátor `<id>` zprávy může být libovolný řetězec, který by měl být v rámci spojení unikátní. Druhá strana použije `<id>` pro identifikaci odpovědi

Druhá strana může odpovědět zprávami **Result (R)**, **Exception (E)** nebo **Execution error (!)**

```
R<id>\b<result data>
E<id>\n<error msg>
?<id>\n<error msg>
```
(samozřejmě druhá straná posílá pouze jednu z uvedených zpráv)

**R result** - úspěšné vykonání metody
**E exception** - metoda byla vykonána, ale byla ukončena výjimkou
**! execution error** - chyba nastala před exekucí metody, tzn, metoda nebyla vůbec vykonána. Typickou chybou je "Method not found" při pokusu zavolat neexistující metodu


### Publisher-Subscriber

Protokol neobsahuje zprávu pro subscribe. Předpokládá se, že subscribe se provede pomocí Request-Response. Během tohoto procesu si musí strany domluvit topic což je
jednoduchý textový identifikátor (neprázdný). Doporučuje se, aby topic byl vybrán subscriberem. Avšak je také možné, aby název topicu vynucoval publisher, je třeba ale aby subscriber znal tento topic před tím, než se k topicu přihlásí (kvůli race condition)

K aktualizaci topicu se použije zpráva **Topic update (T)**

```
T<id>\n<data>
```

K odhlášení topic posílá subscriber zprávu **Unsubscribe (U)**

```
U<id>
```

Implementace peerů v C++ a v JS typicky vyžadují registraci topicu na přijímací straně. Pokud je obdržen **T** na topic, který není registrován, vždy příjemce odesílá **U** aby topic odhlásil

Topic taky může být uzavřen ze strany publishera přes zprávu **Topic close (Z)**

```
Z<id>
```

### Callbacky

Callback je ad-hod vytvořené volání metody aka request-response. Nejčastěji se callback používá pro volání opačným směrem. Pokud jedna strana nabízí služby ve formě RPC a druhá strana je vyvolává, pak callback je opačné volání kdy strana která nabízí služby chce zaslat request na stranu, která služby vyvolává. Avšak není to povinností to takto používat

Callback je třeba registrovat a přiřadit mu `<id>` - následně se toto `<id>` předává protistraně, která následně použije toto ID k volání callbacku

```
C<id>\n<data>
```
Strana která přijme tuto zprávu najde registrovaný callback pod danným `<id>` a ten zavolá, předá mu data. Callback pak musí vygenerovat zprávu **R**, **E** nebo **?** jako u zprávy **Method call (M)**


```
R<id>\b<result data>
E<id>\n<error msg>
?<id>\n<error msg>
```
(samozřejmě druhá straná posílá pouze jednu z uvedených zpráv)

**R result** - úspěšné vykonání callbacku a předání výsledku
**E exception** - callback byl vykonán a skončil výjimkou
**! execution error** - chyba nastala před exekucí callbacku, tedy callback nebyl vyvolán

Callback lze zavolat pouze jednou, po vyvolání se dané `<id>` zneplatní a nelze jej zavolat znovy. Pro řetězové volání callbacků musí po každém zavolání volaná strana registrovat nový callback a předat nové `<id>`

Příklad

```
peer1: M<id1>\n<method>\n<data ... <id2> ....>
peer2: R<id1>\n<data>

peer2: C<id2>\n<data>
peer1: R<id2>\n<data>
```

**Poznámka**: Při volbě `<id>` je třeba dbát na to, že se toto `<id>` sdílí s `<id>` pro volání metod. Proto je vhodné pro ID volání metod zvolit jiný systém genrování než pro callbacky. Ta ID měla být unikátní. Nicméně je třeba si uvědomit, že `<id>` callbacku volí druhá strana spojení, než strana, která následně může narazit na problém unikátnosti IDček. 

Doporučuje se tedy prefixovat ID. MethodCall "mXXX", Callback "cXXX"

```
peer1: Mm01\n<method>\n<data ... c01 ....>
peer2: Rm01\n<data>

peer2: Cc01\n<data>
peer1: Rc01\n<data>
```

## Proměnné kontextu spojení

Obě strany mohou definovat libovolnou proměnnou a přiřadit jí hodnotu. Ta se pak stává součástí kontextu spojení. Kontext spojení existuje dokud není spojení uzavřeno (tím dojde ke smazání proměnných).

Proměnné mohou být použity například nastavení identifikačních tokenů. Není tedy nutné implementovat metody zajišťující nějakou formu autentifikace nebo autorizace, jednoduše jedna strana nastaví domluvenou proměnnou na očekávanou hodnotu a druhá strana si ji může zkontrolovat.

Název proměnné je identifikátor složený z libovolných znaků s kódem 32 výše. V názvu proměnné se tedy nesmí objevit řídící znaky (0-31). Jméno proměnné se u zprávy uvádí za typem zprávy, oddělovače je znak nové řádky

```
   1b                 vary            1b               vary
+-----+------+------+--//---+------+------+------+------+------+------+ ...
|  S  |            identifier      |  \n  |     ... value ...
+-----+------+------+--//---+------+------+------+------+------+------+ ...

```

Příklad

```
Stoken
wdjqdh423fdbcw3847fb3.wehf739fgwe
```

Obě strany mají vlastní namespace. Rozlišuje se `local` a `remote` namespace. Zpráva **S** se používá pro přenos hodnoty proměnné z `local` namespace jednoho peera do `remote` namespace druhého peera. Namespace `local` může být čteno a zapisováno, namespace `remote` může být pouze čteno.

### Smazání proměnné.

Ke smazání proměnné se používá zpráva **X**.

```
Xtoken
```

## Routování

Protokol neřeší routování zpráv přímo, pouze se jedná o sadu doporučení. Adresace zpráv zde probíhá víceméně pouze zkrze názvy metod. 

Doporučení: Název metody může obsahovat **namespace** oddělené znakem `:`

```
Account:getBalance
Account:getTransaction
Stock:listTrades
```

(nicméně, volba oddělovače není vynucena).

Typicky serverové peery mohou volání metody přeposílat na další peery, které je zpracují. Z výše uvedeného například `Account:getBalance` zpracovává router pro `Account`, který osloví patříčného peera, s voláním metody `getBalance`.

## Nápověda '?'

Zpráva '?' slouží k dotazování informací o systému, ke kterému je peer připojen. Nejčastěji k listování jednotlivých služeb, které protějšek nabízí. Používá se podobně jako 
zpráva **M**

```
peer1: ?<id>\n<method>
peer2: R<id>\n<content>
```

Vrácený `<content>` má pevně definovanou strukturu, aby bylo možno výsledky strojově zpracovat

### Dotaz na seznam metod

```
?<id>\n
```

Tento dotaz vrátí seznam metod nabízený protějším peerem - v následující formě

```
Mlogin
Mlogout
MgetInfo
RAccount:
RStock:

```
* **M** - method - název metody
* **R** - route - název trasy (namespace). Objevuje se i s oddělovačem 

### Dotaz na metody dané trasy

```
?<id>\n
Account:
```

Tento dotaz vrací data ve stejném formátu jako **Dotaz na seznam metod**. Seznam neobsahuje název trasy 

```
R<id>
MgetBalance
MgetTransaction
```

### Dokumentace k metodě

Každá metoda může nabízet dokumentaci pro uživatele v lidsky čitelném formátu

```
?<id>\n
Account:getBalance
```

```
R<id>
D<text>
```

Dokumentace vždy začíná písmenem D (tím se liší od seznamů) následovaný libovolným textem až do konce zprávy


## Definice formátů zpráv

### ! - Execution error

```
!<id>
<code> <message>
```

### ? - Method help

```
?<id>
<method_name>
```

### C - Callback call

```
C<id>
<callback_id>
<arguments>
```

### E - Exception

```
E<id>
<exception description>
```

### H - Hello message

```
H<version>
<payload>
```

### M - Method call

```
M<id>
<method_name>
<arguments>
```

### R - Result

```
R<id>
<result>
```

### S - Var set
```
S<varname>
<value>
```

### T - Topic update

```
T<topic_id>
<payload>
```

### U - Unsubscribe

```
U<topic_id>
```

### W - Welcome message

```
W<version>
<payload>
```

### X - Var unset

```
X<varname>
```

### Z - Topic close

```
Z<topic_id>
```

