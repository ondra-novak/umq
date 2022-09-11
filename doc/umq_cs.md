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

**Poznámka**: Popis UMQ rámce není kompletní, rozšíření rámce - viz **attachment**

## Typy zpráv

* **!** - Execution error
* **?** - Method discover
  **-**   Attachment error
* **A** - Attachment
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
!<id>\n<error msg>
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

Callback je třeba registrovat a přiřadit mu `<cb_id>` - následně se toto `<cb_id>` předává protistraně, která následně použije toto ID k volání callbacku

```
C<id>\n<cb_id>\n<data>
```
**Poznámka:** Položka `<id>` v tomto případě je volena volajícím, stejně jako v případě volání metody **M**. Toto *id* následně identifikuje odpověď.

Strana která přijme tuto zprávu najde registrovaný callback pod danným `<cb_id>` a ten zavolá, předá mu data. Callback pak musí vygenerovat zprávu **R**, **E** nebo **!** jako u zprávy **Method call (M)**


```
R<id>\b<result data>
E<id>\n<error msg>
!<id>\n<error msg>
```
(samozřejmě druhá straná posílá pouze jednu z uvedených zpráv)

**R result** - úspěšné vykonání callbacku a předání výsledku
**E exception** - callback byl vykonán a skončil výjimkou
**! execution error** - chyba nastala před exekucí callbacku, tedy callback nebyl vyvolán

Callback lze zavolat pouze jednou, po vyvolání se dané `<cb_id>` zneplatní a nelze jej zavolat znovy. Pro řetězové volání callbacků musí po každém zavolání volaná strana registrovat nový callback a předat nové `<cb_id>`

Příklad

```
peer1: M<id1>\n<method>\n<data ... <id2> ....>
peer2: R<id1>\n<data>

peer2: C<id3>\n<id2>\n<data>
peer1: R<id3>\n<data>
```


Callbacky lze používat k implementaci patternu **push-pull**

```
peer1: M<id1>
       ReadyToWork
       My ID is <id2>

peer2: R<id1>
       Accepted

peer2: C<id3>
       <id2>
       There is some work

peer1: R<id3>
       Done, there goes a result
       
```
      


## Proměnné kontextu spojení

Obě strany mohou definovat libovolnou proměnnou a přiřadit jí hodnotu. Ta se pak stává součástí kontextu spojení. Kontext spojení existuje dokud není spojení uzavřeno (tím dojde ke smazání proměnných).

Proměnné mohou být použity například nastavení identifikačních tokenů. Není tedy nutné implementovat metody zajišťující nějakou formu autentifikace nebo autorizace, jednoduše jedna strana nastaví domluvenou proměnnou na očekávanou hodnotu a druhá strana si ji může kdykoliv později zkontrolovat, přičemž dotaz na proměnnou je instantní, jelikož se provádí lokálně.

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

## Attachmenty

Attachment představuje binární obsah propojený z danou zprávou. Je to jediný způsob, jak předávat binární obsah zkrze textové zprávy.

Binární obsah se přenáší pomocí binárního rámce. Binární rámce nemají žádnou identifikaci. 

Attachment není celá zpráva, jedná se o prefix existující zprávy - rozšiřuje tedy protokol o možnost přidat ke zprávě attachment. Parametrem prefixu A je počet binárních rámců, které jsou
svázané s toutu zprávou

```
A<cnt>
<typ><id>
<data>
```

Příklad - zavolání metody s binárním obsahem. Příklad může představovat jednoduchý upload
binárního obsahu jako soubor `obr.jpg`. Samotný binární obsah je poslán za zprávou.

```
==== TEXT ====
A1
M123
upload
obr.jpg
==== BIN ====
 ...binarní obsah <obr.jpg> ...
==== TEXT ====
 ...další rámce...
```

Velikost attachmentu není nijak omezena, vše záleží na konkrétní platformě. 

### Pravidla pro binární rámce

Binární rámce jsou zasílány nezávisle na textových rámcích. Musí pouze platit, že binární rámec určité zprávy se může poslat až po odeslání vlastní zprávy. Nemusí to však být hned
následující rámec. Pokud jiné zprávy mají binární rámce, pak se rámce řadí do fronty a odesílají se postupně, přičemž mezi binární rámce je možné vložít libovolné textové rámce, které přenos binárních rámců neovlivní.

Pokud zpráva deklaruje několik attachmentů, jsou tyto attachmenty zařazeny do fronty a 
odesílány postupně jako binární rámce. Stejným způsobem jsou potom vyzvedávány a přiřazovány ke zprávám, které na ně čekají.

Z hlediska aplikačního rozhraní bývají attachmenty řešeny jako promise/future - v javascriptu lze využít async-await na čtení attachmentu. Při přeposílání zpráv mezi peery lze nechat odeslat zprávu s attachmenty aniž by vlastní attachmenty byly staženy.

### Zpráva '-' attachment error

Protože lze attachment ohlásit před tím, než je skutečně získán, může se stát, že attachment nakonec není možné doručit, protože při jeho získávání došlo k chybě. Tato zpráva, která se posílá jako textová zpráva, nahrazuje attachment, který se očekával. Tato zpráva nemá žádné id, a pouze v datové části obsahuje text chyby

```
==== TEXT ====
A1
M123
upload
obr.jpg
==== TEXT ====
-
Image retrival error, connection lost
```

Chyba se následně propaguje jako exception která vyskočí při pokusu získat danný attachment


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

## Discover '?'

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

Tato chyba informuje o tom, že došlo k chybě při hledání nebo routování volání. Volající
má jistotu, že nedošlo k vyvolání metody - metoda nebyla nalezena, vyvolána, nebo nebyla
nalezena cesta, nebo peer obsluhující dané volání není k dispozici


### ? - Method discover

```
?<id>
<method_name>
```

Tato zpráva je podobná jako volání metody 'M' s tím, že metodu nevolá, ale dotazuje se na její popis. Pokud není zadané jméno metody, pak jde o dotaz na seznam metod nabízeného protějším uzlem. Pokud je zadáno jméno trasy (route), pak jde o dotaz na seznam metod nabízené na zadané trase

Odpověď příjde jako **R** nebo jako **E**. Odpověď pro **R** má předepsanný formát.

Popis metody se vrací s prefixem **D**. Zbytek zprávy obsahuje popis

```
D<popis>\n<popis>\n<popis>
```

Seznamy jsou položky oddělené novým řádkem. Před názvem je prefix specifikující typ položky

* **M** - metoda
* **R** - trasa

```
Mmethod1
Mmethod2
Rroute1
Rroute2
```

### A - Attachment

```
A<id>
<associated_message>
```

Zpráva **A** není samostatnou zprávou, ale pouze prefixem, který se objevuje před zprávou. Přítomnost tohoto prefixu znamená, že součástí zprávy je binární obsah, který se posílá jako binární zpráva. Za písmenem **A** se uvádí číselné ID binární zprávy

```
A123
Mxyz
upload_file
file.txt

<binary content>
```

### C - Callback call

```
C<id>
<callback_id>
<arguments>
```

Představuje volání callbacku. **<callback_id>** představuje dopředu známé dočasné ID. Toto ID lze použít k jednomu volání. Odesílatel musí zvolit náhodné **<id>** pod kterým pak přijde odpověď  **R**, **E**, nebo **!**

Odpověď je **R** výsledek, **E** výjimka, **!** v případě, že **<callback_id>** není registrováno.

### E - Exception

```
E<id>
<exception description>
```

Zprává **E** má dvě podoby. Peer většinou posílá **E** bez `<id>`, pokud se jendá o chybu komunikace a téměř vždy odesílatel uzavírá spojení.

Peer posílá **E** s `<id>` metody, která způsobila výjimku. Pak se jedná o výjimku spojenou s danou metodou. Spojení zůstává aktivní.

### H - Hello message

```
H<version>
<payload>
```

Tato zpráva musí být zaslána klientem, který se připojil na server, před zahájením komunikace, kdy potom dojde ke zrušení těchto počítečních rolí.

Klient posílá číslo nejvyšší verze, kterou podporuje. Server může verzi snížit ve své odpovědi **W**. Server může na tuto zprávu odpovědět **E** pokud nechce komunikovat

Součástí zprávy může být aplikačně definovaný payload



### M - Method call

```
M<id>
<method_name>
<arguments>
```

Jedná se o volání metody **<method_name>**. Volající musí generovat unikátní **<id>**. Druhá strana odpovídá pomocí zprávy **R** nebo **E** nebo **!**

### R - Result

```
R<id>
<result>
```

Tato zpráva obsahuje výsledek některého volání, ať už metody **M**, callbacku **C** nebo discovery **?**. 


### S - Var set
```
S<varname>
<value>
```

Zprávu posílá vždy ta strana, která potřebuje nastavit proměnnou v remote namespace protějška. Na zprávu neexistuje odpověď, vždy se předpokládá, že zpráva dorazila a nastavila danou proměnnou

Peer většinou nesleduje změny danné proměnné, pouze se může dotazovat na proměnnou v okamžiku kdy to potřebuje. Pro sledování nějakého topicu se spíš hodí zpráva **T**


### T - Topic update

```
T<topic_id>
<payload>
```

Představuje aktualizaci nějakého topicu. **<id>** by si měl volit subscriber a oznamuje ho publisheru např pomocí zprávy **M**. Pod tímto id se pak posílají topic updates

### U - Unsubscribe

```
U<topic_id>
```

Ukončí daný topic ze strany subscribera. Publisher by měl přestat posílat **T**. 
Běžně se ale stává, že po odeslání této zprávy dorazí ještě několik zpráv **T** než informace o odhlášení probublá publisherem až ke zdroji

Kód peeru často eviduje seznam přijímaných topiců. Pokud je přijat topic, který není registrován, automaticky se odesílá zpráva **U**. 


### W - Welcome message

```
W<version>
<payload>
```

Odesílá server jako odpověď na zprávu **H**. Server zkontroluje navrženou verzi. Server navrhnout nižší verzi, pokud navrženou verzi nepodporuje. 

Klient může na zprávu odpovědět **E** pokud odmítne navrženou verzi.

### X - Var unset

```
X<varname>
```

Posílá se ke smazání dané proměnné

### Z - Topic close

```
Z<topic_id>
```

Posílá publisher jako oznámení subscriberovi, že již žádná zpráva **T** k danému topicu již nepřijde. Subscriber zpravidla smaže registraci topicu, takže jakákoliv další zpráva se stejným topicem způsobí odpověď **U**

