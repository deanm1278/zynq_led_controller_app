fs = 24000;
nfft = 4096;

nbands = 20; % number of mel bands

mel = linspace(100, 2800, nbands + 1);
hz = 700 * (exp(mel ./ 1127) - 1);

% figure out the starting bin
bin = round(hz ./ (fs / nfft));