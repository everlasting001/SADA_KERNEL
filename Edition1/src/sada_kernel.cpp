#include "sada_kernel.h"

// Fixed-point square root using integer bit pairs.
static density_t sqrt_variance(variance_t value) {
    ap_uint<96> bits = value.range(95, 0);
    ap_uint<48> root = 0;
    ap_uint<50> remainder = 0;

    for (int i = 47; i >= 0; --i) {
        // Bring down the next two input bits.
        remainder <<= 2;
        remainder.range(1, 0) = bits.range(2 * i + 1, 2 * i);

        ap_uint<50> trial = ap_uint<50>(root);
        trial = (trial << 2) | ap_uint<50>(1);

        root <<= 1;

        if (remainder >= trial) {
            remainder -= trial;
            root[0] = 1;
        }
    }

    // Input has 48 fractional bits; root has 24.
    density_t result = 0;
    result.range(47, 0) = root;
    return result;
}

// mean distance
distance_t mean_squared_distance(
    const distance_t distances[N_CAND]
){
    distance_sum_t sum = 0;

    for (int r = 0;r < N_CAND; r++){
        sum += distances[r];
    }

    return distance_t(sum / N_CAND);
};

density_t estimate_density(distance_t mean_distance){
    const density_t epsilon = density_t(0.000001); // (avoid / 0)

    density_t denominator = density_t(mean_distance) + epsilon;

    return density_t(density_t(1) / denominator);
};

void compute_raw_density(
    const distance_t distances[N_POINTS][N_CAND],
    density_t rho[N_POINTS]
) {
    // Process one current point per iteration.
    for (int i = 0; i < N_POINTS; i++) {
        distance_t mean_distance =
            mean_squared_distance(distances[i]);

        rho[i] = estimate_density(mean_distance);
    }
}

// Average raw densities across all points in one sample.
density_t mean_density(
    const density_t rho[N_POINTS]
){
    density_sum_t sum = 0;
    for (int i = 0; i < N_POINTS; i++) {
        sum += rho[i];
    }
    return density_t(sum / N_POINTS);
};

void density_variance_std(
    const density_t rho[N_POINTS],
    density_t mean,
    variance_t &variance,
    density_t &std_safe
) {
    variance_sum_t sum = 0;

    // Accumulate squared deviations.
    for (int i = 0; i < N_POINTS; i++) {
        density_diff_t diff =
            density_diff_t(rho[i]) - density_diff_t(mean);

        variance_t squared = variance_t(diff * diff);
        sum += squared;
    }

    // Sample variance: divide by N-1.
    variance = variance_t(sum / (N_POINTS - 1));

    density_t raw_std = sqrt_variance(variance);

    // Match rho.std(...).clamp_min(eps).
    const density_t epsilon = density_t(0.000001);
    std_safe = (raw_std < epsilon) ? epsilon : raw_std;
}

void normalize_clip_density(
    const density_t rho[N_POINTS],
    density_t mean,
    density_t std_safe,
    normalized_density_t rho_clip[N_POINTS]
) {
    // Preserve the standard-deviation lower bound.
    const density_t epsilon = density_t(0.000001);
    density_t denominator =
        (std_safe < epsilon) ? epsilon : std_safe;

    const normalization_work_t upper = 3;
    const normalization_work_t lower = -3;

    for (int i = 0; i < N_POINTS; i++) {
        // Convert to signed types before subtraction.
        density_diff_t diff =
            density_diff_t(rho[i]) - density_diff_t(mean);

        normalization_work_t normalized =
            normalization_work_t(
                diff / density_diff_t(denominator)
            );

        // Explicitly clip before narrowing the output.
        if (normalized > upper) {
            rho_clip[i] = normalized_density_t(3);
        } else if (normalized < lower) {
            rho_clip[i] = normalized_density_t(-3);
        } else {
            rho_clip[i] = normalized_density_t(normalized);
        }
    }
}

void select_density_refs(
    index_t point_id,
    const normalized_density_t rho_clip[N_POINTS],
    const index_t candidate_idx[N_CAND],
    index_t active_idx[N_REFS]
){
    static_assert(
        N_REFS == N_NEAR + N_DENSITY,
        "Density selection requires 8 references."
    );

    density_score_t scores[N_CAND];
    bool used[N_CAND];

    for (int r = 0; r < N_NEAR; r++) {
        active_idx[r] = candidate_idx[r];

        used[r] = true;
    }   // 先取四个最近的

    for (int r = N_NEAR; r < N_CAND; r++){
        index_t j = candidate_idx[r];

        density_delta_t delta = 
            density_delta_t(rho_clip[point_id]) -
            density_delta_t(rho_clip[j]);
        
        if (delta < 0) {
            scores[r] = density_score_t(-delta);
        } else {
            scores[r] = density_score_t(delta);
        }  // 绝对值化

        used[r] = false;    // 还没有选过的点
    }   // 再按照密度选择

    // Select the largest unused score four times.
    for (int s = 0; s < N_DENSITY; s++) {
        int best_pos = N_NEAR;
        density_score_t best_score = 0;
        bool found = false;

        for (int r = N_NEAR; r < N_CAND; r++) {
            if (!used[r] &&
                (!found || scores[r] > best_score)) {
                best_pos = r;
                best_score = scores[r];
                found = true;   // found 保证即使全部相等也能找到
            }
        }

        active_idx[N_NEAR + s] = candidate_idx[best_pos];
        used[best_pos] = true;
    }
}

void select_all_density_refs(
    const normalized_density_t rho_clip[N_POINTS],
    const index_t candidate_idx[N_POINTS][N_CAND],
    index_t active_idx[N_POINTS][N_REFS]
) {
    for (int i = 0; i < N_POINTS; i++) {
        select_density_refs(
            index_t(i),
            rho_clip,
            candidate_idx[i],
            active_idx[i]
        );
    }
}

void density_selector(
    const distance_t distances[N_POINTS][N_CAND],
    const index_t candidate_idx[N_POINTS][N_CAND],
    index_t active_idx[N_POINTS][N_REFS]
) {
    density_t rho[N_POINTS];
    normalized_density_t rho_clip[N_POINTS];

    // Estimate raw density for all points.
    compute_raw_density(distances, rho);

    // Compute per-sample statistics.
    density_t mean = mean_density(rho);

    variance_t variance = 0;
    density_t std_safe = 0;

    density_variance_std(
        rho, mean, variance, std_safe
    );

    // Normalize and clip before comparing densities.
    normalize_clip_density(
        rho, mean, std_safe, rho_clip
    );

    // Select eight reference IDs for every point.
    select_all_density_refs(
        rho_clip, candidate_idx, active_idx
    );
}

// plif neutrons
spike_t plif_step(
    fixed_t input,
    fixed_t lambda,
    fixed_t threshold,
    fixed_t &membrane
){  
    // decay_input=True, v_reset=0
    fixed_t charged = membrane + lambda * (input - membrane);

    spike_t spike = (charged >= threshold);

    membrane = spike ? fixed_t(0) : charged;
    
    return spike;
};

spike_t attention_unit(
    spike_t q,
    const spike_t k_refs[N_REFS],
    spike_t v,
    fixed_t lambda,
    fixed_t threshold,
    fixed_t &membrane,
    count_t &mismatch_count
){
    count_t sum = 0;

    for (int r = 0; r < N_REFS; r++) {
        spike_t different = q ^ k_refs[r];
        sum += different;
    }

    mismatch_count = sum;

    // w
    spike_t gate = plif_step(
        fixed_t(sum), lambda, threshold, membrane
    );

    return v & gate;    // repalce dot products with AND gates
};

// a single point
spike_t attention_point(
    spike_t q,
    const spike_t k_all[N_POINTS],
    const index_t candidate_idx[N_CAND],
    spike_t v,
    fixed_t lambda,
    fixed_t threshold,
    fixed_t &membrane,
    count_t &mismatch_count
){
    spike_t k_refs[N_REFS];

    // Select the nearest references from sorted candidates.
    for (int r = 0; r < N_REFS; r++) {
        index_t j = candidate_idx[r];   // 从候选中选出第r个
        k_refs[r] = k_all[j];
    }

    return attention_unit(
        q, k_refs, v,
        lambda, threshold,
        membrane, mismatch_count
    );
};

// a single channel
void attention_channel(
    const spike_t q[N_POINTS],
    const spike_t k[N_POINTS],
    const spike_t v[N_POINTS],
    const index_t candidate_idx[N_POINTS][N_CAND],
    fixed_t lambda,
    fixed_t threshold,
    fixed_t membrane[N_POINTS],
    spike_t output[N_POINTS],
    count_t mismatch_count[N_POINTS]
){
    for (int i = 0; i < N_POINTS; i++) {
        output[i] = attention_point(
            q[i],
            k,
            candidate_idx[i],
            v[i],
            lambda,
            threshold,
            membrane[i],
            mismatch_count[i]
        );
    }
};

// a single frame
void attention_frame(
    const spike_t q[N_CHANNELS][N_POINTS],
    const spike_t k[N_CHANNELS][N_POINTS],
    const spike_t v[N_CHANNELS][N_POINTS],
    const index_t candidate_idx[N_POINTS][N_CAND],
    fixed_t lambda,
    fixed_t threshold,
    fixed_t membrane[N_CHANNELS][N_POINTS],
    spike_t output[N_CHANNELS][N_POINTS],
    count_t mismatch_count[N_CHANNELS][N_POINTS]
){
    for (int c = 0; c < N_CHANNELS; c++) {
        attention_channel(
            q[c],
            k[c],
            v[c],
            candidate_idx,
            lambda,
            threshold,
            membrane[c],
            output[c],
            mismatch_count[c]
        );
    }
};

void sada_attention_kernel(
    const distance_t distances[N_POINTS][N_CAND],
    const index_t candidate_idx[N_POINTS][N_CAND],
    const spike_t q[N_CHANNELS][N_POINTS],
    const spike_t k[N_CHANNELS][N_POINTS],
    const spike_t v[N_CHANNELS][N_POINTS],
    fixed_t lambda,
    fixed_t threshold,
    fixed_t membrane[N_CHANNELS][N_POINTS],
    spike_t output[N_CHANNELS][N_POINTS],
    count_t mismatch_count[N_CHANNELS][N_POINTS]
) {
    index_t active_idx[N_POINTS][N_REFS];

    // Select references once; share them across channels.
    density_selector(
        distances, candidate_idx, active_idx
    );

    for (int c = 0; c < N_CHANNELS; c++) {
        for (int i = 0; i < N_POINTS; i++) {
            spike_t k_refs[N_REFS];

            // Gather K using the density-selected point IDs.
            for (int r = 0; r < N_REFS; r++) {
                index_t j = active_idx[i][r];
                k_refs[r] = k[c][j];
            }

            // XOR reduction, PLIF, and current-point V gating.
            output[c][i] = attention_unit(
                q[c][i],
                k_refs,
                v[c][i],
                lambda,
                threshold,
                membrane[c][i],
                mismatch_count[c][i]
            );
        }
    }
}