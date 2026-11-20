# 도움말 글 근거 (M6b P11, P15에서 리드 + 항목으로 다시 나눔. 게임에는 안 나와요)

`help_ko.json`의 주제마다 리드(L)와 항목(1, 2, …) 순서대로, 근거가 된 명세 파일과 절을 적어요. 명세는 `docs/spec/`(main 저장소)예요.
P15에서 한 덩어리 글을 "리드 한 줄 + 짧은 항목"으로 나눴어요. 사실은 그대로이고 새로 넣은 내용은 없어요.
모델에 없는 현실 효과는 글에 넣지 않았어요(아래 "뺀 것").

## 1. 딸기가 자라는 데 필요한 것 (growth)
- L·1. 빛 → 광합성, 피복 겹마다 빛 줄어듦: strawberry-minimal "2. 생장", house-climate "1. 피복과 창"
- 2. 잎 면적·온도 곡선: strawberry-minimal "2"
- 3. 밤 유지 호흡으로 순생장 음수·기관 축소: strawberry-fruit "3"
- 4. 스트레스 중 최솟값(보통 모드), 유효 양분만 흡수: strawberry-minimal "1. 스트레스 계수", "3. 흡수"
- 5. 단일·저온에 꽃눈, 적산 온도로 개화·익음, 자동 수확: strawberry-phenology "2", "3"; strawberry-fruit "5", "6"
- 6. 개화기 온도와 꽃가루받이, 기형과, 과실 수 많으면 작아짐: strawberry-fruit "2", "4"
- 7. 고사 경로는 뿌리 건강 하나(과습·지온·아질산·산성), 병의 뿌리 피해: strawberry-minimal "5"; soil-microbes; foliar-disease 4.2

## 2. 물과 흙 (water)
- L·1. 포장용수량 넘는 물 배수: soil-water "배수", aeration "수분 명세 변경"
- 2. 증발은 일사·젖음에 비례, 잎 그늘이 줄임: soil-water "증발"
- 3. 건조 → 흡수 감소, EC 상승: strawberry-minimal "3"; soil-chemistry "EC"
- 4·5. 포화 가까울수록 통기 감소, 다짐이 통기 상한을 낮춤: aeration "통기 지수"
- 6. 관수로 다짐, 경운으로 풀림·작물 있으면 뿌리 피해, 유기물이 통기 상한을 올림: aeration "다짐 변화", "통기 지수"
- 7. 배수와 함께 용탈(질산이 쉽게): nitrogen "용탈"

## 3. 질소 (nitrogen)
- L. 질소 형태 변화: nitrogen "상태 변수"
- 1. 분해 → 암모늄, 두 단계 질산화: organic-matter "분해"; nitrogen "질산화"
- 2. 온도·수분·통기가 질산화 속도: nitrogen "질산화"
- 3. 질산 용탈, 통기 부족 시 탈질: nitrogen "용탈"; denitrification "프로세스"
- 4. 아질산 축적 조건과 뿌리 독성: nitrogen "질산화", "아질산 독성 판정"; strawberry-minimal "5"
- 5·6. 부족 → 생장 감소, 과다 → 꽃눈 지연·당도·경도 저하·병 증가: strawberry-minimal "1"; strawberry-phenology "2"; strawberry-fruit "5"; foliar-disease 4.1
- 7. 높은 C:N 퇴비의 질소 고정: organic-matter "분해", 퇴비 정의

## 4. 그 밖의 양분 (nutrients)
- L. 양분마다 충분도 → 생장: strawberry-minimal "1"
- 1. 인 고정, 산성·알칼리에서 빠름: phosphorus "목적", "프로세스"
- 2·3. 칼륨 부족 → 당도, 칼슘 부족 → 경도: strawberry-fruit "목적", 파라미터 표
- 4·5. 칼륨·암모늄 ↔ 칼슘·마그네슘 흡수 길항(마그네슘이 더), 교환 자리에서 밀려나 용탈: cation-exchange "흡수 길항", "평형 분배"; m3-overview S5
- 6. 망간: 산성·과습에서 용출 → 독성, 알칼리에서 결핍: manganese "목적", "1. 산화·환원", "4. 흡수"

## 5. 흙의 산도와 염류 (soil_chem)
- L·1. 양분별 pH 유효도 곡선: soil-chemistry "유효도 곡선", "유효량"
- 2. 강산성 뿌리 피해, 망간 용출: soil-chemistry "산성 장해"; manganese
- 3. 석회 용해로 pH 상승·칼슘 공급: soil-chemistry "석회 용해 → pH 갱신"
- 4. 질산화·암모늄 흡수로 산성화, 질산 흡수로 반대: soil-chemistry; nitrogen M2 메모; strawberry-minimal "3"
- 5. 염류(EC) → 생장·흡수·증산 감소, 딸기는 약함, 마르면 상승: soil-chemistry "EC"; strawberry-minimal "1·3·4"
- 6. CEC(점토 + 안정 부식), 낮으면 크게 흔들림: cation-exchange "목적", "CEC"; m3-overview S6
- 7. 시비 직후 큰 관수의 용탈: cation-exchange "평형 분배"

## 6. 퇴비와 흙 속 생물 (soil_life)
- L·1. 분해 → 양분·열·CO2, 일부 부식 → CEC: organic-matter "프로세스"; cation-exchange "CEC"; soil-microbes
- 2. 덜 익은 퇴비(빠른 분해·열·암모늄), 완숙 퇴비(느림): organic-matter "퇴비 정의"
- 3. 길항균은 유기물에서 잘 살고 감염을 줄임, 유익균 투입은 낮은 흙에서 크게: soil-microbes "무리" 표, "4. 유익균 투입"
- 4. 푸사리움(따뜻·산성), 역병균(과습·서늘): soil-microbes "1~3"
- 5. 감염은 되돌아가지 않고 병원균을 흙에 더함: soil-microbes; m5-overview
- 6. 약제 소독은 유익 무리·질산화균도 죽임, 태양열 소독은 길항균이 잘 남음: disinfection "2", "3"
- 7. 소독 뒤 암모늄 축적·약제 가스 잔류 → 바로 정식하면 뿌리 피해: disinfection "4·5"; m5-overview 3장

## 7. 하우스 공기: 온도 (temperature)
- L·1. 일사 가열, 닫힌 하우스·초가을 고온: house-climate "2. 평형 기온과 난방"
- 2. 피복 보온과 투과율 감소: house-climate "1. 피복과 창"
- 3. 겹·좌우 측창, 모든 겹이 열려야 환기, 양쪽이 더: house-climate "1"
- 4. 난방기 설정 온도·최대 출력, 창 열면 연료 증가·미달: house-climate "2", "명령"
- 5. 온도 → 광합성·꽃가루받이·익음·당도·경도: strawberry-minimal; strawberry-fruit "2", "5"
- 6. 저온 피해(잎·꽃·과실 손실): house-climate "4. 저온 피해"

## 8. 하우스 공기: 습도와 이슬 (humidity)
- L·7. 연속 젖음 시간 → 잿빛곰팡이·탄저, 짧은 마름은 끊지 않음: house-humidity 4.3; foliar-disease 4.2, 4.3
- 1. 수증기 원천(낮 증산 + 토양 증발), 멀칭이 증발 줄임: house-humidity 4.2, 5; strawberry-minimal "4. 증산"
- 2. 피복 안쪽 결로, 난방은 습도 낮춤, 겹 많으면 습도 높음: house-humidity 4.2
- 3. 한낮 환기는 밤 습도를 거의 안 바꿈, 저녁 환기: house-humidity 1, 4.2
- 4. 잎 젖음(어두움·포기 사이 습도), 빽빽함·정체 공기: house-humidity 4.3
- 5. 유동팬·햇빛이 말림: house-humidity 4.1, 4.3
- 6. 점적 없으면 머리 위 관수 → 잎 젖음·튐: house-humidity 4.4

## 9. 하우스 공기: 이산화탄소 (co2)
- L·5. 광합성에 CO2 반응 곡선(부족 시 크게 감소, 많을 때 조금 증가): strawberry-fruit "3"; house-climate "3. CO2 수지"
- 1. 맑은 겨울 낮 닫힌 하우스에서 몇 시간 안에 떨어짐: house-climate "3" 방향
- 2. 환기로 회복, 조금 열어도 회복·열 손실 적음: house-climate "CO2", "3"
- 3·4. 밤 호흡으로 바깥보다 높음, 퇴비 분해가 보탬: house-climate "3" 방향

## 10. 잎과 열매의 병 (disease)
- L·1. 세 병, 토양 무기태 질소가 감염을 높임, 낮은 등급 모종의 잠복 감염: foliar-disease 2, 4.1
- 2. 탄저: 따뜻할 때, 머리 위 관수로 튐, 관부 → 뿌리 → 고사: foliar-disease 4.2
- 3. 멀칭: 흙 → 포기 경로 막음, 포기 → 포기 튐 늘림: foliar-disease 4.2
- 4. 잿빛곰팡이: 포자, 알맞은 온도, 연속 젖음, 꽃·익은 과실, 포자원: foliar-disease 4.3
- 5. 흰가루: 습하지만 젖지 않은 공기, 어린 잎, 광합성 감소, 과실 상품성 상실: foliar-disease 4.4
- 6. 젖음 줄이기(잿빛곰팡이·탄저): foliar-disease 1; house-humidity 4.3
- 7. 질소 줄이기, 뽑기로 근원 제거, 빈자리: foliar-disease 4.1, 4.6

## 11. 응애와 천적 (mites)
- L·1. 응애 온도 반응: foliar-disease 3.5, 4.5
- 2. 밀도의 두 주 평균으로 광합성 감소 → 피해가 남음: foliar-disease 4.5
- 3. 칠레이리응애 방사, 포식으로 증식, 응애 많은 포기로 이동: foliar-disease 4.5
- 4. 천적 활동 온도대, 고온에서 못 따라감: foliar-disease 4.5
- 5. 알 부화에 며칠 평균 습도 필요: foliar-disease 4.5
- 6. 먹이 없으면 굶어 사라짐 → 재발, 재방사: foliar-disease 4.5
- 7. 늦은 방사는 잃은 수확을 못 되돌림: foliar-disease 4.5 / 시나리오

## 12. 조작법
`help_ko.json`의 `keys` 표 (화면 아래 키 안내 줄도 같은 표에서 만들어요).

## 뺀 것 (모델에 없어서 쓰지 않은 현실 효과)
- 휴면, 런너(기는줄기), 화방 차이, 꽃가루 매개 곤충(늘 있다고 봐요), 과실 색·신맛, 붕소 기형, 손 수확, 잎 따기, 과숙: strawberry-phenology / strawberry-fruit 단순화
- 잎 끝 타기(칼슘), 노란 잎 같은 눈에 보이는 결핍 증상, 칼슘이 증산 따라 움직이는 것: cation-exchange·m3/m4 단순화
- 암모니아 가스 손실, 가스 장해: nitrogen / organic-matter 단순화
- 망간과 칼슘·마그네슘의 흡수 경쟁, 망간 과잉에 따른 철 결핍: manganese 단순화
- 인 과잉의 직접 피해(균근이 줄어드는 것만 있어요), 유기태 인이 천천히 풀리는 것: phosphorus 단순화
- 칼륨이 점토 속에 갇히는 것, 나트륨·염소: cation-exchange / soil-chemistry 단순화
- 습도에 따라 증산이 줄어드는 되먹임(되돌림 결정), 머리 위 관수가 하우스 습도를 올리는 것, 결로 낙수 피해, 제습기·포그·차광 커튼: house-humidity 4.5, 10
- 유동팬이 온도에 주는 효과, 보온 커튼, 바람: house-climate 단순화; house-humidity 4.1
- CO2 시비 장치(M7): house-climate 단순화
- 살균제·살충제, 이웃 거리에 따른 전염, 과실 탄저·잎 반점, 마른 잎 잔재: foliar-disease 10
- 천적이 바깥에서 들어오는 것, 응애 세대·휴면, 질소가 응애에 주는 영향: foliar-disease 10; m5-overview 결정 49

## 확신이 없었던 것
- "가장 모자란 것이 빠르기를 붙잡아요"는 보통 모드 기준이에요. 어려움 모드는 모자란 것들을 모두 곱해요(strawberry-minimal "1").
- 잿빛곰팡이 표(foliar-disease 1)는 "아침·한낮 환기"를 쓰지만 house-humidity는 한낮 환기가 밤 습도를 거의 안 바꾼다고 해요. 글은 house-humidity 쪽을 따랐어요.
- 소독 뒤 아질산 축적은 알칼리 흙에서만 한계를 넘는다는 결정(m5-overview 결정 13·22)을 따라 "질소가 바뀌지 못하고 쌓인다"고만 썼어요.
- 흰가루병의 약한 빛 효과는 봄에는 거의 의미가 없어서(m5-overview 15.3) 글에 넣지 않았어요.
