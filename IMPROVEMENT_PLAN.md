# Fullteq 개선 계획 — 다단계 물리적 회로 모델링 아키텍처 반영

참고: "Pultec EQ의 물리적 회로 모델링: 다단계 아키텍처를 통한 VST3 플러그인 설계 가이드" (EON LLM wiki)

## 현재 구현 상태 대비 격차 (Gap Analysis)

| 가이드 권장 스테이지 | 현재 구현 | 격차 |
|---|---|---|
| 패시브 LC 필터 네트워크 (부하 관계 포함) | RBJ peaking biquad 4단 직렬 | 부하 상호작용 미반영, boost/cut이 수학적 병렬 합성에 가까움 |
| insertion loss 보상 | 없음 | 패시브 손실 + 튜브 메이크업 게인 구조 없음 |
| 튜브 앰프 비선형 (2차/3차 고조파) | Pade tanh 소프트 클립 (drive>0일 때만) | 2차 고조파 없음, drive 기본값 0이라 기본 사운드가 깨끗한 디지털 |
| 트랜스포머 포화/공명 | 없음 | 전무 |
| LC 리조넌트 고역 응답 | bandwidth로 Q만 조정 | 인덕터 공명의 넓고 부드러운 피크 특성 부족 |
| UI 실시간 응답 그래프 | EQP-1A 패널 리디자인 시 그래프 제거됨 | 가이드가 강조하는 Boost/Cut 상호작용 시각화 부재 |
| 파라미터 매핑 | 선형 | 주파수 계열은 로그 매핑이 자연스러움 |

## 개선 항목 (우선순위 순)

### Phase 1 — 저역 Pultec trick의 물리적 재현 (최우선)
가이드 핵심: Boost와 Cut은 순차 직렬 연결이며, Cut이 Boost의 부하(load)가 된다.
동일 주파수에서 동시 사용 시 60Hz 이하만 리프트되고 그 위는 상쇄되는 곡선이 나와야 한다.

- [ ] Boost: low-shelf, Cut: 반대 방향 shelf를 같은 주파수에 직렬 배치
- [ ] Cut 스테이지의 코너를 Boost보다 약간 높게 (부하 효과 근사) — 현재는 동일 주파수 peaking 쌍
- [ ] 검증: 60Hz boost 3dB + atten 3dB에서 20~40Hz는 유지, 60Hz 위는 감쇠하는 곡선 확인 (응답 그래프로)

### Phase 2 — 튜브 스테이지 개선
- [ ] 2차 고조파 추가: 비대칭 웨이브셰이핑 (y = x + k2*x^2 항, DC 오프셋 제거)
- [ ] 메이크업 게인 구조: 패시브 insertion loss 상수 + 튜브 보상 게인으로 재구성
- [ ] Tube drive 기본값을 미세량(예: 0.15)으로, OFF 옵션 유지

### Phase 3 — 고역 LC 리조넌트
- [ ] high boost를 peaking 1단 대신 셸빙 + 공명 피크 2단 근사로 교체
- [ ] bandwidth를 SHARP~BROAD 로그 매핑으로 재조정

### Phase 4 — 트랜스포머 모델 (선택)
- [ ] 저주파 굴곡(low-end hump) + 고주파 미세 포화를 갖는 간단한 트랜스포머 모델
- [ ] Bypass 가능한 Transformer 토글 파라미터 추가

### Phase 5 — UI/UX
- [ ] EQP-1A 패널 레이아웃을 유지하면서 그래프 영역 복원 (패널 중앙 상단 좁은 밴드)
- [ ] Boost/Cut 동시 사용 시 그래프에서 상호작용 실시간 시각화
- [ ] 파라미터 로그 매핑 적용

## 진행 상황 (구현 완료)

- [x] Phase 1: 저역 쉘프 2단 (부스트 코너 ×1.0 / 감쇠 코너 ×1.5, 감쇠 게인 ×0.65). 부스트 8 + 감쇠 8 @60 Hz에서 25 Hz +3.8 dB, 110 Hz -2.4 dB, 1 kHz 평탄
- [x] Phase 2: 비대칭 tanh 튜브 (2·3차 고조파, DC 블로커), 4배 오버샘플링, 드라이브 기본 15 %
- [x] Phase 3: 고역 부스트 = 공명 피크 (대역폭 0 SHARP ~ 10 BROAD, 로그 매핑), 감쇠 = ATTEN SEL 하이쉘프
- [x] Phase 4: 트랜스포머 저역 코어 포화 (TUBE / XFMR 토글)
- [x] Phase 5: 응답 그래프 복원, 입력/드라이브/출력 트림
- 검증: `Tests/DspTests.cpp` (빌드: `cmake --build build --target FullteqTests`, 실행: `build/FullteqTests_artefacts/FullteqTests`). 15개 항목 전부 통과
- 미실시: 실제 DAW 로드, pluginval, 청감 비교

## 롤백 방법

모든 DSP 변경은 PluginProcessor.cpp/.h 와 PluginEditor.cpp/.h 에 집중된다.

- 변경 전 백업: `cp -R Source Source.bak-$(date +%Y%m%d-%H%M%S)` (Fullteq 디렉터리에서)
- 롤백: 백업 디렉터리를 Source 로 복구 후 `cmake --build build --parallel 4` 재빌드
- 검증: 빌드 exit code 0 + 설치된 VST3/AU 타임스탬프 갱신 확인