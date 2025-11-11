
#include <klang.h>
using namespace klang::optimised;

///// Phasor oscillator (0 to 1) (aliased)
//struct Phasor : basic::Saw {
//	void process() {
//		Saw::process();
//		out = out * 0.5 + 0.5;
//	}
//};

struct Partial {
	Frequency frequency;
	dB gain;
};

static inline signal saturate(signal in) { return in > 1 ? signal(1) : in < -1 ? signal(-1) : in; }

static const Partial partials[8] = {
	{8.8f, 27.8f}, {16.1f, 33.7f}, {24.9f, 30.3f}, {30.8f, 28.8f}, 
	{45.4f, 26.1f}, {67.4f, 22.0f}, {87.9f, 32.1f}, {98.1f, 24.6f}, 
};

//-----------------------------
// Pedal thump (2 pulses / rev)
//-----------------------------
struct Pedal : Generator {
    Phasor phasor;
    BPF bpf;

    Pedal() { bpf.set(100, 0.1); }
	virtual ~Pedal() { }

    void set(param speed) {
        phasor.set(speed ); // two thumps per crank cycle
    }

    void process() {
        signal s = sin(phasor * pi * 2);
//        s = s > 0.99f ? s : signal(0);  // crude impulse
        s * s >> out;
    }
};

struct Chain : Modifier {
	Sine pressure;
	Noise noise;
	BPF noise_bpf[3];
	param rate;
	
	Chain(){
		noise_bpf[0].set(2550, 5);
		noise_bpf[1].set(4250, 15);
		noise_bpf[2].set(6500, 25);
//		pressure.set(0, 0.5
	}
	virtual ~Chain(){ }
	
	void set(param speed){
		rate = speed * 22;
		pressure.set(rate);
	}
	
	void process() {
		signal n = (noise >> noise_bpf[0]) * (0.1 + in * in * 0.05) +( noise >> noise_bpf[1]) * (0.5) + (noise >> noise_bpf[2]);
		(0.5 + in * 0.25) * (n ^ 2) * pressure(rate * (0.9 + in * 0.2)) >> out;
		out = saturate(out * 1) * 5; 
		
		if(rate < 10)
			out *= rate * 0.1;
	}	
};

struct Wheel : Generator {
	Sine osc[11];
	Amplitude osc_gain[11];
	
	Noise noise;
	BPF noise_bpf;
	
	BPF osc_bpf; 
	
	Pulse pulse;
	param rate;
	param tick_gain;
	
	Delay<512> delay;

	Wheel(){
		for(int p=0; p<8; p++)
			osc_gain[p] = dB(partials[p].gain - 38) -> Amplitude;
		
		pulse.set(partials[0].frequency, 0, 0.1); // set duty cycle
		
		noise_bpf.set(5887, 1.2);
		osc_bpf.set(12);
	}
	virtual ~Wheel() {}
	
	void set(param speed){
		rate = speed / partials[0].frequency;
		tick_gain = rate ^ 3;
		
		for(int p=0; p<8; p++)
			osc[p].set(partials[0].frequency * (p+1) * rate);
		
		pulse.set(partials[0].frequency * rate);
	}

	void process() {
		signal n = noise;
		
		n = n * 0.5 + (n >> noise_bpf) * 0.5;
	
		signal tone = 0;
		for(int o=0; o<8; o++){
			tone += osc[o] * osc_gain[o];
		}
		
		signal filtered = pulse >> osc_bpf;

		out = (tone ^ 4) * min(0.01, rate * 0.005) * n + (filtered ^ 3) * n * tick_gain;
//		out = 0;
	}
	
};

struct Bicycle : Sound {
	Chain chain;
	Wheel wheel;
	Pedal pedal;

	Delay<192000> resonances[3];
	LPF resonance_lpf;

	Noise noise;
	LPF noise_lpf[3];

	BPF bpf;

	signal pedalling = 0;

	// Initialise plugin (called once at startup)
	Bicycle() {
		controls = { 
			Dial("Pedalling", 0.0, 1.0, 0.5),
			Dial("Wheel Speed", 0, 50, 6),
			Dial("Pedal Speed", 0, 4, 1),
		};
	}

	// Prepare for processing (called once per buffer)
	void prepare() {
		
	}

	// Apply processing (called once per sample)
	void process() {
		pedalling += (controls[0].smooth() - pedalling) * 0.001;
		param wheel_speed = controls[1];
		param pedal_speed = controls[2] / 2;
		signal energy = pedal(pedal_speed);

		
		signal wheel_noise = wheel(wheel_speed);
		
		signal chain_noise = 0.7 * (energy >> chain(pedal_speed)) + 0.25 * (wheel_noise);

		

		//signal resonance = 0.5 * chain_noise;
		//resonance = klang::(resonance);
		
		//signal rnd = (noise >> noise_lpf[0](5)) * 0.5 + 0.5;
		//signal fb1 = 0.25 * resonances[0](0.1 * (1 + .1 * rnd * fs));

		//rnd = (noise >> noise_lpf[1](5)) * 0.5 + 0.5;
		//signal fb2 = 0.3 * resonances[1](0.24 * (1 + .1 * rnd * fs));

		//rnd = (noise >> noise_lpf[2](5)) * 0.5 + 0.5;
		//signal fb3 = 0.35 * resonances[2](0.13 * (1 + .1 * rnd * fs));
		//
		//resonance * 0.25 + fb1 + fb3 >> resonances[1];
		//resonance * 0.25 + fb2 + fb1 >> resonances[2];
		//resonance * 0.25 + fb3 + fb2 >> resonances[0];

		//signal resonance = (resonances[0](0.125 * fs) + resonances[1](0.25 * fs) + resonances[2](0.5 * fs)) * 0.3;
		//chain_noise += resonance;
		//	
		//chain_noise = (chain_noise >> resonance_lpf(8000, 1));

		//resonances[0] << chain_noise * 0.9;
		//resonances[1] << chain_noise * 0.9;
		//resonances[2] << chain_noise * 0.9;

		(pedalling * chain_noise + 0.25 * (1 - .75 * pedalling) * wheel_noise >> bpf(11000,1)) * 0.6 >> out;
	}
};